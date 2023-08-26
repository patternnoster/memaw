#pragma once
#include <concepts>

/**
 * @file
 * Implementation details of library concepts
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

template <typename T>
concept nothrow_equality_comparable = std::equality_comparable<T>
  && requires(const std::remove_reference_t<T>& t) {
  {t == t} noexcept;
  {t != t} noexcept;
};

} // namespace memaw::__detail
