#pragma once
#include <atomic128/atomic128_ref.hpp>
#include <type_traits>

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

  [[nodiscard]] void* allocate(size_t, pow2_t) noexcept {
    return nullptr;
  }

  void deallocate(void*, size_t, pow2_t) noexcept {}

  bool operator==(const cache_resource_impl& rhs) const noexcept {
    // While inside constructors & assignment operator we can ignore
    // thread safety, here we can't. Thus, for simplicity:
    return this == &rhs;
  }

private:
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

} // namespace memaw::__detail
