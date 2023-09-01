#pragma once
#include <optional>

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
  constexpr static bool is_granular = true;
  constexpr static bool is_thread_safe = true;

  constexpr os_resource() noexcept = default;

  /**
   * @brief Get the size of a (regular) system memory page (usually
   *        4KiB)
   **/
  static pow2_t get_page_size() noexcept {
    return __detail::os_info.page_size;
  }

  /**
   * @brief Get the size of a (default) big page if it is known and
   *        available on the system
   *
   * On Linux, returns the default big (huge) page size (as given in
   * /proc/meminfo), if any. On Windows returns the minimum big
   * (large) page size available (usually 2MiB). On other systems, may
   * return an empty optional even if big pages are supported.
   *
   * @note  This method returning a value does not itself guarantee a
   *        successful big page allocation (e.g., Windows requires
   *        additional permissions, on Linux preallocated huge pages
   *        must be available etc.)
   **/
  static std::optional<pow2_t> get_big_page_size() noexcept {
    return __detail::os_info.big_page_size;
  }

  /**
   * @brief Returns the known minimum size limit for allocations
   **/
  static pow2_t min_size() noexcept {
    return get_page_size();
  }

  /**
   * @brief Returns the known minimum alignment that any allocated
   *        address will have (regardless of the alignment argument)
   * @note  The result is always >= min_size(), but may be bigger on
   *        some systems (e.g. with regular pages on Windows)
   **/
  static pow2_t guaranteed_alignment() noexcept {
    return __detail::os_info.granularity;
  }

  [[nodiscard]] static void* allocate
    (size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;

  static void deallocate(void* ptr, size_t size,
                         size_t alignment = alignof(std::max_align_t)) noexcept;

  constexpr bool operator==(const os_resource&) const noexcept = default;
};

} // namespace memaw
