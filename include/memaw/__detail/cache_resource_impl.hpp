#pragma once
#include <cmath>
#include <type_traits>
#include <utility>

#include "concepts_impl.hpp"
#include "mem_ref.hpp"

/**
 * @file
 * The implementation of cache_resource
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

template <auto _cfg>
class cache_resource_impl {
public:
  using upstream_t = typename decltype(_cfg)::upstream_resource;

  constexpr static auto thread_safety = __detail::thread_safe;

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
  inline std::pair<void*, size_t> upstream_allocate(size_t, pow2_t) noexcept;

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

  /**
   * @brief Returns the next smallest allocation size that is valid
   *        for the upstream (i.e., is not less than its min_size(),
   *        if it's bound, and a multiple of it if it's granular)
   **/
  static size_t ceil_allocation_size(const size_t size) noexcept {
    if constexpr (granular_resource<upstream_t>) {
      const auto ups_min_size = upstream_t::min_size();
      const auto remainder = size % ups_min_size;
      return size + (remainder > 0) * (ups_min_size - remainder);
    }
    else if constexpr (bound_resource<upstream_t>)
      return nupp::maximum(size, upstream_t::min_size());
    else return size;
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

  struct alignas(16) head_block_t {
    uintptr_t ptr;
    size_t size = 0;

    bool operator==(const head_block_t&) const noexcept = default;
  };
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
std::pair<void*, size_t> cache_resource_impl<_cfg>::upstream_allocate
  (const size_t size, const pow2_t alignment) noexcept {
  // If got here, we'll need to allocate from the upstream. First
  // determine the size we request
  size_t next_size = get_next_block_size();
  size_t next_allocation = ceil_allocation_size(next_size);

  void* result;
  size_t allocation_size =
    next_allocation >= size ? next_allocation : ceil_allocation_size(size);

  for (;;) {
    const auto lbs_ref = make_mem_ref<thread_safety>(last_block_size_);

    result = try_allocate(upstream_, allocation_size,
                          nupp::maximum(alignment, _cfg.granularity));

    if (result) {
      /* Update the next block since we allocated and it succeeded (we
       * won't bother with CAS here, this value is more of a
       * recommendation anyway) */
      lbs_ref.store(next_size, mo_t::relaxed);
      return std::make_pair(result, allocation_size);
    }
    else if (allocation_size > next_allocation)
      // Do not update the lbs if the allocation was overly big
      return {};

    /* On failure, we can try to reduce the block size to a previous
     * value (unless it's smaller than the requested size) until it
     * reaches the allowed minimum */
    if (next_size == _cfg.min_block_size) {
      // Only update if the regular alignment was requested, otherwise
      // the failure may be due to it alone
      if (alignment <= _cfg.granularity)
        lbs_ref.store(0, mo_t::relaxed);  // Start from the beginning
                                          // next time
      return {};
    }

    next_size = get_prev_block_size(next_size);
    allocation_size = next_allocation = ceil_allocation_size(next_size);
    if (allocation_size < size) {  // Can't reduce that much
      if (alignment <= _cfg.granularity)
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

  /* Okay, now load the head. We will use two relaxed atomic reads
   * here, realizing we can end up loading values from different
   * blocks (in which case the first CAS will fix that) */
  auto curr_head = head_block_t {
    .ptr = make_mem_ref<thread_safety>(head_.ptr).load(mo_t::relaxed),
    .size = make_mem_ref<thread_safety>(head_.size).load(mo_t::relaxed)
  };

  for (;;) {
    if (curr_head.size < size || curr_head.ptr % alignment)
      break;  // We will need a direct allocation

    const head_block_t new_head = {
      .ptr = curr_head.ptr + size,
      .size = curr_head.size - size
    };

    if (make_mem_ref<thread_safety>(head_)
        .compare_exchange_weak(curr_head, new_head,
                               mo_t::acquire, mo_t::relaxed))
      // The easy and likely way out
      return reinterpret_cast<void*>(curr_head.ptr);
  }

  // If got here, we'll need to allocate from the upstream
  const auto [result, allocation_size] = upstream_allocate(size, alignment);
  if (!result) return nullptr;  // Couldn't allocate

  // Try to install a new head if it has more free size left than the
  // current one

  const head_block_t next_head = {
    .ptr = uintptr_t(result) + size,
    .size = allocation_size - size
  };

  for (;;) {
    if (curr_head.size >= next_head.size) {
      // The head has more free space: mark the remaining allocated
      // block free
      if (next_head.size)
        deallocate(reinterpret_cast<void*>(next_head.ptr), next_head.size);
      return result;
    }

    // The allocated block has more free space: try to exchange
    if (make_mem_ref<thread_safety>(head_)
        .compare_exchange_weak(curr_head, next_head,
                               mo_t::release, mo_t::relaxed)) {
      // If successful, marks free what's remaining of the old head
      // (if anything)
      if (curr_head.size)
        deallocate(reinterpret_cast<void*>(curr_head.ptr), curr_head.size);

      return result;
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
