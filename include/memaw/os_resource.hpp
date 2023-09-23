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
  constexpr static bool is_sweeping = true;
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

  /**
   * @brief Allocates (full pages of) memory of the given size and
   *        alignment directly from the OS, using pages of the
   *        specified type.
   *
   * The page type can be either:
   * - page_types::regular to request regular memory pages of size
   *   get_page_size();
   * - page_types::big to request the default big memory pages. On
   *   Linux the big (huge) pages of the default size (as possibly
   *   returned from get_big_page_size()) will be requested. Windows
   *   may (or may not) decide for itself what big (large) page sizes
   *   to use (and may even use several at a time) depending on the
   *   size and alignment values. For other systems, no promises of
   *   the real page type(s) are made;
   * - pow2_t with the exact page size value. On Linux (with procfs
   *   mounted at /proc) and Windows 10+ (or Server 2016+) must be one
   *   of the values returned from get_available_page_sizes(). On
   *   other systems may be unsupported (and force the allocation to
   *   fail).
   *
   * On Windows, an attempt to acquire the SeLockMemoryPrivilege for
   * the process will be made if this function is called with a
   * non-regular page type. If that fails (e.g., if the user running
   * the process doesn't have that privilege), all big (large) pages
   * allocations will fail as well.
   *
   * The alignment of the resulting address is at least as big as
   * guaranteed_alignment(page_type). If the requested alignment is
   * greater than that value, then note that:
   * - on Linux the allocation result is always aligned by the page
   *   size used (i.e., get_page_size() for page_types::regular,
   *   *get_big_page_size() for page_types::big and the value of
   *   page_type otherwise), and the allocation with bigger alignment
   *   will fail since we choose to not implicitly allocate additional
   *   memory in that case;
   * - on latest versions of Windows (10+, Server 2016+), any
   *   alignment can be requested independent of the page_type (but
   *   the allocation may still fail if no matching address is
   *   available). On earlier versions, only granularity (as returned
   *   by guaranteed_alignment(page_type)) alignment is supported
   *   (otherwise, the allocation will fail);
   * - on other systems, alignments bigger than the page size may or
   *   may not be supported (but note that if this implementation
   *   doesn't know how to guarantee the requested alignment on the
   *   current system, it will return nullptr without making a system
   *   call).
   *
   * @param size must be a multiple of min_size(page_type), otherwise
   *        the allocation will fail
   * @param alignment must be a power of 2. On some systems (see
   *        above) the allocation will fail if this value is greater
   *        than guaranteed_alignment(page_type)
   * @param page_type can be page_types::regular, page_types::big or
   *        pow2_t with the exact page size value (see above for the
   *        detailed description and limitations)
   **/
  template <page_type P = page_types::regular_t>
  [[nodiscard]] static void* allocate
    (const size_t size, const size_t alignment = alignof(std::max_align_t),
     const P page_type = {}) noexcept {
    return __detail::os_mapper::map(size, pow2_t{alignment}, page_type);
  }

  /**
   * @brief   Deallocates the previously allocated region or several
   *          adjacent regions of memory
   * @param   ptr must be the pointer to the beginning of the (first)
   *          region, returned from a previous call to allocate()
   * @param   size must be the value of the size argument passed to
   *          the allocate() call that resulted in allocation of the
   *          region (in case of a single region deallocation) or the
   *          exact sum of all those arguments (in case of several
   *          adjacent regions deallocation)
   * @param   alignment parameter is always ignored and present only
   *          for interface compatibility reasons
   * @warning All memory in range [ptr, ptr + size) must be accessible
   *          (i.e., not previously deallocated), otherwise the
   *          behaviour is undefined
   **/
  static void deallocate(void* const ptr, const size_t size,
                         const size_t /*alignment, ignored */= 1) noexcept {
    __detail::os_mapper::unmap(ptr, size);
  }

  constexpr bool operator==(const os_resource&) const noexcept = default;
};

} // namespace memaw
