#pragma once
#include <type_traits>

#include "../concepts.hpp"

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
public:
  using upstream_t = R;

  constexpr pool_resource_impl()
    noexcept(std::is_nothrow_default_constructible_v<upstream_t>) = default;

  constexpr pool_resource_impl(upstream_t&& upstream)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    upstream_(std::move(upstream)) {}

  bool operator==(const pool_resource_impl& rhs) const noexcept {
    return this == &rhs;
  }

private:
  upstream_t upstream_;
};

} // namespace memaw::__detail
