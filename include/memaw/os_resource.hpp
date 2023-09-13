#pragma once
#include <nupp/mask_iterator.hpp>
#include <optional>
#include <ranges>

#include "__detail/base.hpp"
#include "__detail/concepts_impl.hpp"
#include "__detail/os_info.hpp"
#include "__detail/os_mapper.hpp"

/**
 * @file
 * The basic memory resource that makes direct system calls
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {

/**
 * @brief Tags for the types of system memory pages available for
 *        allocation
 **/
struct page_types {
  using regular_t = __detail::os_mapper::regular_pages_tag;
  using big_t = __detail::os_mapper::big_pages_tag;

  constexpr static regular_t regular{};
  constexpr static big_t big{};
};

/**
 * @brief The concept of a type that denotes the size of a system
 *        memory page and must be either one of the tags in page_types
 *        or pow2_t for explicit size specification.
 **/
template <typename T>
concept page_type =
  __detail::same_as_either<T, page_types::regular_t, page_types::big_t, pow2_t>;

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
   * @brief Returns all the available page sizes on the system, that
   *        are known and supported in a pow2_t-valued range
   *
   * Always contains the regular page size (as returned from
   * get_page_size()), and the result of get_big_page_size(), if
   * any. On Linux additionally returns all the supported huge page
   * sizes (as listed in /sys/kernel/mm/hugepages/). On other systems,
   * no additional entries are guaranteed, even if supported (but may
   * still appear, like in case of Windows on x86_64 with the pdpe1gb
   * CPU flag)
   *
   * @note  This method enlisting a value does not guarantee a
   *        successful big page allocation of that size
   **/
  static ranges::range auto get_available_page_sizes() noexcept {
    return
      ranges::subrange{nupp::mask_iterator(__detail::os_info.page_sizes_mask),
                       std::default_sentinel};
  }

  /**
   * @brief  Returns the known minimum size limit for allocations with
   *         the specified page type
   * @return * for page_types::regular, get_page_size();
   *         * for page_types::big, *get_big_page_size() if it's
   *           defined, or of get_page_size() if it's not;
   *         * for explicitly specified page size, its value.
   **/
  template <page_type P = page_types::regular_t>
  static pow2_t min_size(const P page_type = {}) noexcept {
    return __detail::os_mapper::get_min_size(page_type);
  }

  /**
   * @brief Returns the known minimum alignment that any allocated
   *        with the specified page type address will have (regardless
   *        of the alignment argument)
   * @note  The result is always >= min_size(page_type), but may be
   *        bigger on some systems (e.g. with regular pages on
   *        Windows)
   **/
  template <page_type P = page_types::regular_t>
  static pow2_t guaranteed_alignment(const P page_type = {}) noexcept {
    return __detail::os_mapper::get_guaranteed_alignment(page_type);
  }

  [[nodiscard]] static void* allocate
    (size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;

  static void deallocate(void* ptr, size_t size,
                         size_t alignment = alignof(std::max_align_t)) noexcept;

  constexpr bool operator==(const os_resource&) const noexcept = default;
};

} // namespace memaw
