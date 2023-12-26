#pragma once
#include <array>
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
private:
  consteval static auto get_chunk_sizes() noexcept;

public:
  using upstream_t = R;

  constexpr static auto chunk_sizes = get_chunk_sizes();

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

} // namespace memaw::__detail
