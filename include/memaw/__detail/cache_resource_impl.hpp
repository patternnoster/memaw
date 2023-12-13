#pragma once
#include <cmath>
#include <type_traits>
#include <utility>

#include "../resource_traits.hpp"
#include "mem_ref.hpp"
#include "resource_common.hpp"

/**
 * @file
 * The implementation of cache_resource
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

struct alignas(16) head_block_t {
  uintptr_t ptr;
  size_t size = 0;
  bool operator==(const head_block_t&) const noexcept = default;
};

template <auto _cfg>
class cache_resource_impl {
public:
  using upstream_t = typename decltype(_cfg)::upstream_resource;

  constexpr static auto thread_safety = _cfg.thread_safe
    ? __detail::thread_safe : __detail::thread_unsafe;

  constexpr cache_resource_impl()
    noexcept(std::is_nothrow_default_constructible_v<upstream_t>) = default;

  constexpr cache_resource_impl(upstream_t&& upstream)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    upstream_(std::move(upstream)) {}

  inline ~cache_resource_impl() noexcept;

  constexpr cache_resource_impl(cache_resource_impl&& rhs)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    upstream_(std::move(rhs.upstream_)) {
    copy_internals(rhs);
    rhs.reset_internals();
  }

  constexpr cache_resource_impl& operator=(cache_resource_impl&& rhs)
    noexcept(std::is_nothrow_move_assignable_v<upstream_t>)
    requires(std::is_move_assignable_v<upstream_t>) {
    if (this != &rhs) {
      upstream_ = std::move(rhs.upstream_);
      copy_internals(rhs);
      rhs.reset_internals();
    }
  }

  [[nodiscard]] inline void* allocate(size_t, pow2_t) noexcept;

  inline void deallocate(void*, size_t, pow2_t = {}) noexcept;

  bool operator==(const cache_resource_impl& rhs) const noexcept {
    // While inside constructors & assignment operator we can ignore
    // thread safety, here we can't. Thus, for simplicity:
    return this == &rhs;
  }

private:
  /**
   * @brief Returns the next block size acquired from configuration
   *        alone (the upstream bounds/granularity not taken into
   *        account)
   **/
  size_t get_next_block_size() const noexcept {
    if constexpr (_cfg.min_block_size == _cfg.max_block_size)
      return _cfg.max_block_size;
    else {
      const size_t value =
        make_mem_ref<thread_safety>(last_block_size_).load(mo_t::relaxed);

      if (!value) return _cfg.min_block_size;
      return nupp::minimum(size_t(double(value) * _cfg.block_size_multiplier),
                           _cfg.max_block_size);
    }
  }

  /**
   * @brief Returns the block size one step smaller than the given one
   *        (the upstream bounds/granularity not taken into account)
   **/
  static size_t get_prev_block_size(const size_t size) noexcept {
    if constexpr (_cfg.min_block_size == _cfg.max_block_size)
      return _cfg.min_block_size;
    else {
      const auto value = size_t(std::ceil(double(size)
                                          / _cfg.block_size_multiplier));
      return nupp::maximum(value, _cfg.min_block_size);
    }
  }

  void copy_internals(const cache_resource_impl& rhs) noexcept {
    head_ = rhs.head_;
    free_chunks_head_ = rhs.free_chunks_head_;
    last_block_size_ = rhs.last_block_size_;
  }

  void reset_internals() noexcept {
    head_ = head_block_t{};
    free_chunks_head_ = nullptr;
    last_block_size_ = 0;
  }

  /**
   * @brief Allocates the given (non-zero) amount of bytes from the
   *        upstream with the regular alignment. On failure returns
   *        the block of size 0
   **/
  inline head_block_t upstream_allocate(size_t) noexcept;

  head_block_t head_;

  struct free_chunk_t {
    free_chunk_t* next;

    // The arguments passed to deallocate():
    size_t size;
    pow2_t alignment;
  };
  free_chunk_t* free_chunks_head_ = nullptr;

  size_t last_block_size_ = 0;
  upstream_t upstream_;

public:
  constexpr static auto min_granularity =
    pow2_t{ sizeof(free_chunk_t), pow2_t::ceil };

  static_assert(min_granularity >= alignof(std::max_align_t));
  static_assert(_cfg.granularity >= min_granularity);
};

template <auto _cfg>
head_block_t cache_resource_impl<_cfg>::upstream_allocate
  (const size_t size) noexcept {
  using traits = resource_traits<upstream_t>;

  // If got here, we'll need to allocate from the upstream. First
  // determine the size we request
  size_t next_size = get_next_block_size();
  size_t next_allocation = traits::ceil_allocation_size(next_size);

  size_t allocation_size = next_allocation >= size
    ? next_allocation : traits::ceil_allocation_size(size);

  for (;;) {
    const auto lbs_ref = make_mem_ref<thread_safety>(last_block_size_);
    const auto result = memaw::allocate<exceptions_policy::nothrow>
      (upstream_, allocation_size, _cfg.granularity);

    if (result) {
      /* Update the next block since we allocated and it succeeded (we
       * won't bother with CAS here, this value is more of a
       * recommendation anyway) */
      lbs_ref.store(next_size, mo_t::relaxed);
      return head_block_t{ uintptr_t(result), allocation_size };
    }
    else if (allocation_size > next_allocation)
      // Do not update the lbs if the allocation was overly big
      // (this only happens during the first iteration)
      return {};

    /* On failure, we can try to reduce the block size to a previous
     * value (unless it's smaller than the requested size) until it
     * reaches the allowed minimum */
    if (next_size == _cfg.min_block_size) {
      lbs_ref.store(0, mo_t::relaxed);  // Start from the beginning
                                        // next time
      return {};
    }

    next_size = get_prev_block_size(next_size);
    allocation_size = next_allocation = traits::ceil_allocation_size(next_size);
    if (allocation_size < size) {  // Can't reduce that much
      lbs_ref.store(next_size, mo_t::relaxed);
      return {};
    }
  }
}

template <auto _cfg>
void* cache_resource_impl<_cfg>::allocate(const size_t size,
                                          const pow2_t alignment) noexcept {
  // First make sure the size if a multiple of granularity (it is a
  // pow2_t, so the check is easy)
  if (size % _cfg.granularity) [[unlikely]] return nullptr;

  // Prepare some lambdas for the future
  const auto do_deallocate = [this](const uintptr_t ptr, const size_t size) {
    if (size)
      deallocate(reinterpret_cast<void*>(ptr), size, _cfg.granularity);
  };

  /* Okay, now load the head. We will use two relaxed atomic reads
   * here, realizing we can end up loading values from different
   * blocks (in which case the first CAS will fix that) */
  auto curr_head = head_block_t {
    .ptr = make_mem_ref<thread_safety>(head_.ptr).load(mo_t::relaxed),
    .size = make_mem_ref<thread_safety>(head_.size).load(mo_t::relaxed)
  };

  for (;;) {
    const auto [result, padding] = align_pointer(curr_head.ptr, alignment);
    const head_block_t new_head = {
      .ptr = result + size,
      .size = curr_head.size - size - padding
    };

    if (new_head.ptr > curr_head.ptr + curr_head.size)
      break;  // We will need a direct allocation

    if (make_mem_ref<thread_safety>(head_)
        .compare_exchange_weak(curr_head, new_head,
                               mo_t::acquire, mo_t::relaxed)) {
      // The easy and likely way out
      do_deallocate(curr_head.ptr, padding);
      return reinterpret_cast<void*>(result);
    }
  }

  // If got here, we'll need to allocate from the upstream
  const auto required_size = size +  // Also require max padding bytes
    (alignment > _cfg.granularity ? (alignment - _cfg.granularity) : 0);

  const auto new_head = upstream_allocate(required_size);
  if (!new_head.size) return nullptr;  // Couldn't allocate

  // Try to install a new head if it has more free size left than the
  // current one

  const auto [result, padding] = align_pointer(new_head.ptr, alignment);
  do_deallocate(new_head.ptr, padding);

  const head_block_t next_head = {
    .ptr = result + size,
    .size = new_head.size - size - padding
  };

  for (;;) {
    if (curr_head.size >= next_head.size) {
      // The head has more free space: mark the remaining allocated
      // block free
      do_deallocate(next_head.ptr, next_head.size);
      return reinterpret_cast<void*>(result);
    }

    // The allocated block has more free space: try to exchange
    if (make_mem_ref<thread_safety>(head_)
        .compare_exchange_weak(curr_head, next_head,
                               mo_t::release, mo_t::relaxed)) {
      // If successful, marks free what's remaining of the old head
      // (if anything)
      do_deallocate(curr_head.ptr, curr_head.size);
      return reinterpret_cast<void*>(result);
    }
  }
}

template <auto _cfg>
void cache_resource_impl<_cfg>::deallocate(void* ptr, const size_t size,
                                           const pow2_t alignment) noexcept {
  if (!ptr || !size) [[unlikely]] return;

  const auto head_ref = make_mem_ref<thread_safety>(free_chunks_head_);

  auto chunk = new (ptr) free_chunk_t {
    .next = head_ref.load(mo_t::relaxed),
    .size = size,
    .alignment = alignment
  };

  /* Just keep the free chunks on the stack. Note that since the
   * memory is only released in the destructor, we don't have to deal
   * with the ABA problem here */
  while (!head_ref.compare_exchange_weak(chunk->next, chunk,
                                         mo_t::release, mo_t::relaxed));
}

template <auto _cfg>
cache_resource_impl<_cfg>::~cache_resource_impl() noexcept {
  /* The chunks in the free list are obviously out of order, so we
   * find andjacent ones here. Luckily, this is the destructor so
   * we don't have to think about thread safety */
  free_chunk_t* regions;  // Sorted processed part

  if (head_.size)  // "deallocate" head
    regions = new (reinterpret_cast<void*>(head_.ptr)) free_chunk_t {
      .next = nullptr,
      .size = head_.size,
      .alignment = pow2_t{}
    };
  else regions = nullptr;

  for (auto chunk = make_mem_ref<thread_safety>(free_chunks_head_)
         .load(mo_t::acquire); chunk;) {
    const auto chunk_begin = uintptr_t(chunk);
    const auto chunk_end = chunk_begin + chunk->size;

    const auto next_chunk = chunk->next;

    // This works like insertion sort, only we merge the adjacent
    // regions together
    free_chunk_t* prev_region = nullptr;  // null or < chunk_begin
    free_chunk_t* next_region = regions;  // null or > chunk_begin
    while (next_region && chunk_begin > uintptr_t(next_region)) {
      prev_region = next_region;
      next_region = next_region->next;
    }

    // We either put chunk between prev and next or merge 2 or 3 of
    // them together

    if (uintptr_t(next_region) == chunk_end) {
      // Merge with next (never 0 here)
      chunk->size+= next_region->size;
      chunk->next = next_region->next;
      next_region->~free_chunk_t();
    }
    else chunk->next = next_region;

    if (prev_region) {
      const auto prev_region_end = uintptr_t(prev_region) + prev_region->size;

      if (prev_region_end == chunk_begin) {
        // Merge with prev
        prev_region->size+= chunk->size;
        prev_region->next = chunk->next;
        chunk->~free_chunk_t();
      }
      else prev_region->next = chunk;
    }
    else regions = chunk;

    chunk = next_chunk;
  }

  // Finally pass merged adjacent regions to deallocate()
  for (auto region = regions; region;) {
    free_chunk_t curr_chunk = *region;
    region->~free_chunk_t();

    try_deallocate(upstream_, region, curr_chunk.size, curr_chunk.alignment);
    region = curr_chunk.next;
  }
}

} // namespace memaw::__detail
