#pragma once
#include <utility>

#include "base.hpp"

/**
 * @file
 * Some common detail structures that come in handy in resource
 * implementations
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

/**
 * @brief Aligns the given pointer (up) by the given alignment and
 *        returns the new pointer value and the number of bytes used
 *        as padding
 **/
constexpr std::pair<uintptr_t, size_t> align_pointer
  (const auto ptr, const pow2_t alignment) noexcept {
  const uintptr_t ptr_uint = ptr;

  const auto result = (ptr_uint + alignment.get_mask()) & ~alignment.get_mask();
  const auto padding = result - ptr_uint;
  return std::make_pair(result, padding);
}

} // namespace memaw::__detail
