#pragma once
#include <atomic128/atomic128_ref.hpp>
#include <type_traits>
#include <utility>

#include "base.hpp"

/**
 * @file
 * The implementation of cache_resource
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

using namespace atomic128;

template <auto _cfg>
class cache_resource_impl {
public:
  using upstream_t = typename decltype(_cfg)::upstream_resource;

  constexpr cache_resource_impl()
    noexcept(std::is_nothrow_default_constructible_v<upstream_t>) = default;

  constexpr cache_resource_impl(upstream_t&& upstream)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    upstream_(std::move(upstream)) {}

  ~cache_resource_impl() noexcept {}

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

  void deallocate(void*, size_t, pow2_t = {}) noexcept {}

  bool operator==(const cache_resource_impl& rhs) const noexcept {
    // While inside constructors & assignment operator we can ignore
    // thread safety, here we can't. Thus, for simplicity:
    return this == &rhs;
  }

private:
  [[nodiscard]] auto upstream_allocate(size_t, pow2_t) noexcept {
    return std::make_pair<void*, size_t>(nullptr, 0);
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

  static_assert(_cfg.granularity >= min_granularity);
};

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
    .ptr = std::atomic_ref(head_.ptr).load(mo_t::relaxed),
    .size = std::atomic_ref(head_.size).load(mo_t::relaxed)
  };

  for (;;) {
    if (curr_head.size < size || curr_head.ptr % alignment)
      break;  // We will need a direct allocation

    const head_block_t new_head = {
      .ptr = curr_head.ptr + size,
      .size = curr_head.size - size
    };

    if (atomic128_ref(head_)
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
    if (atomic128_ref(head_)
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

} // namespace memaw::__detail
