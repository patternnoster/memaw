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

  constexpr pool_resource_impl(pool_resource_impl&& rhs)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    chunk_stacks_(std::move(rhs.chunk_stacks_)),
    upstream_(std::move(rhs.upstream_)) {}

  [[nodiscard]] inline void* allocate(size_t, pow2_t) noexcept;
  void deallocate(uintptr_t, size_t) noexcept {}

  bool operator==(const pool_resource_impl& rhs) const noexcept {
    return this == &rhs;
  }

private:
  /**
   * @return the real alignment of any chunk with the given size id
   **/
  static pow2_t get_chunk_alignment(const size_t id) noexcept {
    const auto max_pow2_div = pow2_t{1} << std::countr_zero(chunk_sizes[id]);
    const auto upstream_alignment =
      nupp::maximum(_cfg.min_chunk_size,
                    resource_traits<R>::guaranteed_alignment());
    return nupp::minimum(max_pow2_div, upstream_alignment);
  }

  struct chunk_t {
    chunk_t* next;
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

            const auto [result, padding] = align_pointer(chunk, alignment);

            if (padding) deallocate(uintptr_t(chunk), padding);
            deallocate(result + size, chunk_sizes[stack_id] - padding - size);

            return reinterpret_cast<void*>(result);
          }
        }
        while (++stack_id < chunk_stacks_.size());
        break;  // Haven't found a non-empty stack. Proceed.
      }
    }
  }

  return nullptr;
}

} // namespace memaw::__detail
