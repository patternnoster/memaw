#pragma once
#include <concepts>

#include "base.hpp"
#include "os_info.hpp"

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
  static pow2_t get_min_size(const PageType page_type) noexcept {
    if constexpr (std::same_as<PageType, regular_pages_tag>)
      return os_info.page_size;
    else if constexpr (std::same_as<PageType, pow2_t>)
      return page_type;
    else
      return os_info.big_page_size.value_or(os_info.page_size);
  }

  template <typename PageType>
  [[nodiscard]] inline static void* map(size_t size, pow2_t alignment,
                                        PageType) noexcept;

  inline static void unmap(void*, size_t size, pow2_t alignment) noexcept;
};

} // namespace memaw::__detail
