#pragma once
#include "__detail/base.hpp"
#include "__detail/os_info.hpp"

/**
 * @file
 * The basic memory resource that makes direct system calls
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {

/**
 * @brief Memory resource that always allocates and frees memory via
 *        direct system calls to the OS
 **/
class os_resource {
public:
  constexpr os_resource() noexcept = default;

  /**
   * @brief Get the size of a (regular) system memory page (usually
   *        4KiB)
   **/
  static size_t get_page_size() noexcept {
    return __detail::os_info.page_size;
  }

  [[nodiscard]] static void* allocate
    (size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;

  static void deallocate(void* ptr, size_t size,
                         size_t alignment = alignof(std::max_align_t)) noexcept;

  constexpr bool operator==(const os_resource&) const noexcept = default;
};

} // namespace memaw
