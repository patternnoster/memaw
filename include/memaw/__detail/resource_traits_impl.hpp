#pragma once
#include <concepts>
#include <new>
#include <nupp/algorithm.hpp>

#include "../concepts.hpp"

/**
 * @file
 * Implementation details of resource traits
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

/**
 * @brief Returns the next smallest allocation size that is valid for
 *        resource R (i.e., is not less than its min_size(), if it's
 *        bound, and a multiple of it if it's also granular)
 **/
template <resource R>
constexpr static size_t ceil_allocation_size_impl(const size_t size) noexcept {
  if constexpr (granular_resource<R>) {
    const auto min_size = R::min_size();
    const auto remainder = size % min_size;
    return size + (remainder > 0) * (min_size - remainder);
  }
  else if constexpr (bound_resource<R>)
    return nupp::maximum(size, R::min_size());
  else return size;
}

} // namespace memaw::__detail
