#pragma once
#include <concepts>
#include <nupp/algorithm.hpp>

#include "base.hpp"
#include "environment.hpp"
#include "os_info.hpp"

#if MEMAW_IS(OS, WINDOWS)
#  include <windows.h>
#endif

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
  static pow2_t get_guaranteed_alignment(const PageType page_type) noexcept {
#if MEMAW_IS(OS, WINDOWS)
    if constexpr (std::same_as<PageType, regular_pages_tag>)
      return os_info.granularity;
    else
      return nupp::maximum(get_min_size(page_type), os_info.granularity);
#else
    return get_min_size(page_type);
#endif
  }

  template <typename PageType>
  [[nodiscard]] inline static void* map(size_t, pow2_t, PageType) noexcept;

  inline static void unmap(void*, size_t size) noexcept {
  }

#if MEMAW_IS(OS, WINDOWS)
private:
  inline static bool try_acquire_lock_privilege() noexcept;

  /* NB: in fact, we only need this for big pages. However, keeping it
   * templated allows us to make an acquire call only if big pages
   * allocation was requested (i.e., the corresponding template method
   * was instantiated) */
  template <typename>
  inline static const bool has_lock_privilege = try_acquire_lock_privilege();
#endif
};

template <typename PageType>
[[nodiscard]] void* os_mapper::map(const size_t size, const pow2_t alignment,
                                   const PageType page_type) noexcept {
  constexpr bool RegularPages = std::same_as<PageType, regular_pages_tag>;
  constexpr bool BigPages = std::same_as<PageType, big_pages_tag>;
  constexpr bool ExplicitPageSize = std::same_as<PageType, pow2_t>;
  static_assert(RegularPages || BigPages || ExplicitPageSize);

  // Sanitize the parameters first
  const size_t min_size = get_min_size(page_type);
  if (size < min_size) return nullptr;

  if constexpr (ExplicitPageSize) {
    if (page_type == os_info.page_size) [[unlikely]]
      // In case the user is messing with us trying to allocate
      // regular pages with this complicated interface
      return map(size, alignment, os_mapper::regular_pages_tag{});
  }

  // Now, action!
#if MEMAW_IS(OS, WINDOWS)
  if constexpr (!RegularPages)
    if (!os_info.big_page_size || !has_lock_privilege<big_pages_tag>)
      return nullptr;

  return nullptr;
#else
  return nullptr;
#endif
}

#if MEMAW_IS(OS, WINDOWS)
bool os_mapper::try_acquire_lock_privilege() noexcept {
  /* We don't try to adjust account privileges here, hoping the user
   * already took care of that. We only need to enable the privilege
   * for the process token. Since Windows 10 that doesn't require
   * admin rights anymore (assuming the corresponding account
   * privilege has been granted) */
  HANDLE token;
  TOKEN_PRIVILEGES priv;

  if (!LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME,
                            &priv.Privileges[0].Luid)
      || !OpenProcessToken(GetCurrentProcess(),
                           TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &token))
    return false;  // No need to worry about CloseHandle here

  priv.PrivilegeCount = 1;
  priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

  const bool adjusted =
    AdjustTokenPrivileges(token, false, &priv, 0, nullptr, nullptr)
    && GetLastError() == ERROR_SUCCESS;  // Error_success... Who doesn't love
                                         // Windows and its little corks
  CloseHandle(token);

  return adjusted;
}
#endif

} // namespace memaw::__detail
