#pragma once
#include <array>
#include <bit>
#include <nupp/algorithm.hpp>
#include <type_traits>

#include "../resource_traits.hpp"
#include "resource_common.hpp"
#include "stack.hpp"

/**
 * @file
 * The implementation of pool_resource
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

template <sweeping_resource R, auto _cfg>
class pool_resource_impl {
private:
  consteval static auto get_chunk_sizes() noexcept;

public:
  using upstream_t = R;

  constexpr static auto chunk_sizes = get_chunk_sizes();
  constexpr static auto thread_safety = _cfg.thread_safe
    ? thread_safe : thread_unsafe;

  constexpr pool_resource_impl()
    noexcept(std::is_nothrow_default_constructible_v<upstream_t>) = default;

  constexpr pool_resource_impl(upstream_t&& upstream)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    upstream_(std::move(upstream)) {}

  inline ~pool_resource_impl() noexcept;

  constexpr pool_resource_impl(pool_resource_impl&& rhs)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    chunk_stacks_(std::move(rhs.chunk_stacks_)),
    upstream_(std::move(rhs.upstream_)) {}

  [[nodiscard]] inline void* allocate(size_t, pow2_t) noexcept;
  inline void deallocate(uintptr_t, size_t) noexcept;

  bool operator==(const pool_resource_impl& rhs) const noexcept {
    return this == &rhs;
  }

private:
  /**
   * @return the real alignment of any allocation from the upstream
   **/
  static pow2_t get_upstream_alignment() noexcept {
    return nupp::maximum(_cfg.min_chunk_size,
                         resource_traits<R>::guaranteed_alignment());
  }

  /**
   * @return the real alignment of any chunk with the given size id
   **/
  static pow2_t get_chunk_alignment(const size_t id) noexcept {
    const auto max_pow2_div = pow2_t{1} << std::countr_zero(chunk_sizes[id]);
    return nupp::minimum(max_pow2_div, get_upstream_alignment());
  }

  inline void* allocate_from_block(void*, size_t, size_t, pow2_t) noexcept;

  struct chunk_t {
    chunk_t* next;
    size_t size;  // used in the destructor
  };
  std::array<stack<chunk_t, thread_safety>,
             chunk_sizes.size()> chunk_stacks_ = {};

  static_assert(sizeof(chunk_t) <= _cfg.min_chunk_size);

  upstream_t upstream_;
};

template <sweeping_resource R, auto _cfg>
consteval auto pool_resource_impl<R, _cfg>::get_chunk_sizes() noexcept {
  // Compute log base the multiplier
  static_assert(_cfg.chunk_size_multiplier > 0);
  constexpr auto log_mult = [](const size_t value) noexcept {
    size_t result = 0;
    for (size_t prod = 1; prod < value; ++result) {
      if (_cfg.chunk_size_multiplier < 2) break;
      prod*= _cfg.chunk_size_multiplier;
    }
    return result;
  };

  // Pre-calculate the number of chunk sizes
  constexpr size_t count = 1 +
    log_mult(_cfg.max_chunk_size / _cfg.min_chunk_size);

  // Now actually fill and return (we will make sure it ends with
  // max_chunk_size in the outer interface)
  std::array<size_t, count> result = { _cfg.min_chunk_size };

  for (size_t i = 1; i < result.size(); ++i)
    result[i] = result[i-1] * _cfg.chunk_size_multiplier;

  return result;
}

template <sweeping_resource R, auto _cfg>
void* pool_resource_impl<R, _cfg>::allocate_from_block
  (void* const block_ptr, const size_t block_size,
   const size_t size, const pow2_t alignment) noexcept {
  // Assumption: the block_size is enough to contain size including
  // possible alignment padding
  const auto [result, padding] = align_pointer(block_ptr, alignment);

  // The padding bytes and the tail might be repurposed for further
  // blocks (since size, alignment and padding are all multiples of
  // min_chunk_size)

  if (padding) deallocate(uintptr_t(block_ptr), padding);

  const auto left = block_size - padding - size;
  if (left) deallocate(result + size, left);

  return reinterpret_cast<void*>(result);
}

template <sweeping_resource R, auto _cfg>
void* pool_resource_impl<R, _cfg>::allocate(const size_t size,
                                            const pow2_t alignment) noexcept {
  if (size % _cfg.min_chunk_size) [[unlikely]] return nullptr;

  // First determine the stack we might try to take the memory from
  for (size_t stack_id = 0; stack_id < chunk_sizes.size(); ++stack_id) {
    const auto& chunk_size = chunk_sizes[stack_id];
    if (chunk_size >= size) {
      // Okay, this looks acceptable, check the alignment
      const auto chunk_alignment = get_chunk_alignment(stack_id);

      // Make sure either the alignment is enough or we can compensate
      // for it with (reusable) padding
      if (chunk_alignment >= alignment
          || (alignment - chunk_alignment) <= chunk_size - size) {
        // Okay, we have detirmined minimum stack_id by now. Pick a
        // non-empty stack now
        do {
          if (auto chunk = chunk_stacks_[stack_id].pop()) {
            // Found our guy
            chunk->~chunk_t();
            return allocate_from_block(chunk, chunk_sizes[stack_id],
                                       size, alignment);
          }
        }
        while (++stack_id < chunk_stacks_.size());
        break;  // Haven't found a non-empty stack. Proceed.
      }
    }
  }

  // If got here, we haven't found the proper non-empty
  // stack. Allocate directly from the upstream
  const auto max_padding =
    alignment - nupp::minimum(alignment, get_upstream_alignment());

  const auto req_size =
    nupp::maximum(max_padding + size,
                  _cfg.max_chunk_size * _cfg.chunk_size_multiplier);

  const auto [alloc_result, alloc_size] =
    allocate_at_least<exceptions_policy::nothrow>(upstream_, req_size,
                                                  _cfg.min_chunk_size);
  if (!alloc_result) [[unlikely]] return nullptr;
  return allocate_from_block(alloc_result, alloc_size, size, alignment);
}

template <sweeping_resource R, auto _cfg>
void pool_resource_impl<R, _cfg>::deallocate(const uintptr_t ptr,
                                             const size_t size) noexcept {
  if (!ptr || size < _cfg.min_chunk_size) [[unlikely]]
    return;  // Don't bother with bad params

  // Break the block [ptr, ptr+size) into stack chunks prioritizing
  // bigger ones
  const auto upper_ptr = uintptr_t(ptr);
  size_t upper_size = 0;

  // First determine the biggest possible size of chunk, taking
  // alignment into consideration (everything above it is "upper")
  uintptr_t lower_ptr = upper_ptr;
  size_t lower_size = size;

  size_t curr_id = 1;
  for (size_t i = chunk_sizes.size(); i > 1;) {
    const auto& chunk_size = chunk_sizes[--i];
    if (chunk_size > size) continue;

    const auto [ptr, padding] = align_pointer(upper_ptr,
                                              get_chunk_alignment(i));
    if (chunk_size + padding <= size) {
      // Found it
      upper_size = padding;
      lower_size-= padding;

      lower_ptr = ptr;
      curr_id = i + 1;
      break;
    }
  }

  // Now go through remaining stacks: we can add some chunks from the
  // upper and some from the lower part
  for (size_t i = curr_id; i > 0;) {
    const auto& chunk_size = chunk_sizes[--i];

    // Upper part: go from the bottom
    while (upper_size >= chunk_size) {
      upper_size-= chunk_size;
      const auto new_chunk =
        new (reinterpret_cast<void*>(upper_ptr + upper_size)) chunk_t;
      chunk_stacks_[i].push(new_chunk);
    }

    // Lower part: go from the top
    while (lower_size >= chunk_size) {
      const auto new_chunk = new (reinterpret_cast<void*>(lower_ptr)) chunk_t;
      chunk_stacks_[i].push(new_chunk);

      lower_ptr+= chunk_size;
      lower_size-= chunk_size;
    }
  }
}

template <sweeping_resource R, auto _cfg>
pool_resource_impl<R, _cfg>::~pool_resource_impl() noexcept {
  // First sort and merge all the regions from stacks
  chunk_t* merged_head = nullptr;
  for (size_t i = 0; i < chunk_stacks_.size(); ++i)
    merged_head = merge_chunks(chunk_stacks_[i].reset(),
                               chunk_sizes[i], merged_head);

  // Now deallocate one by one
  while (merged_head) {
    // We always request the same alignment, but the real allocation
    // size can actually be ceilled in case the upstream reasource is
    // bound or granular
    const auto size =
      resource_traits<upstream_t>::ceil_allocation_size(merged_head->size);
    const auto next_head = merged_head->next;
    merged_head->~chunk_t();

    memaw::deallocate<exceptions_policy::nothrow>
      (upstream_, merged_head, size, _cfg.min_chunk_size);

    merged_head = next_head;
  }
}

} // namespace memaw::__detail
