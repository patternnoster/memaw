#pragma once
#include "base.hpp"

/**
 * @file
 * The OS allocation methods gathered into one (static) class
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

class os_mapper {
public:
  struct regular_pages_tag {};
  struct big_pages_tag {};

  template <typename PageType>
  [[nodiscard]] inline static void* map(size_t size, pow2_t alignment,
                                        PageType) noexcept;

  inline static void unmap(void*, size_t size, pow2_t alignment) noexcept;
};

} // namespace memaw::__detail
