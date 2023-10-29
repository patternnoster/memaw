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

  constexpr static auto min_granularity = pow2_t{ alignof(std::max_align_t) };

  constexpr cache_resource_impl()
    noexcept(std::is_nothrow_default_constructible_v<upstream_t>) = default;

  constexpr cache_resource_impl(upstream_t&& upstream)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    upstream_(std::move(upstream)) {}

  ~cache_resource_impl() noexcept {}

  constexpr cache_resource_impl(cache_resource_impl&& rhs)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    upstream_(std::move(rhs.upstream_)) {}

  constexpr cache_resource_impl& operator=(cache_resource_impl&& rhs)
    noexcept(std::is_nothrow_move_assignable_v<upstream_t>)
    requires(std::is_move_assignable_v<upstream_t>) {
    if (this != &rhs) {
      upstream_ = std::move(rhs.upstream_);
    }
  }

  [[nodiscard]] void* allocate(size_t, pow2_t) noexcept {
    return nullptr;
  }

  void deallocate(void*, size_t, pow2_t) noexcept {}

  bool operator==(const cache_resource_impl&) const noexcept {
    return true;
  }

private:
  upstream_t upstream_;
};

} // namespace memaw::__detail
