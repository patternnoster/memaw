#pragma once
#include <concepts>
#include <nupp/algorithm.hpp>

#include "base.hpp"
#include "environment.hpp"
#include "os_info.hpp"

#if MEMAW_IS(OS, WINDOWS)
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#  if MEMAW_IS(OS, APPLE)
#    include <mach/vm_statistics.h>
#  endif
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

  /* We use the VirtualAlloc2 function and the extended parameters for
   * two things: an explicitly specified page size or the alignment
   * greater than guaranteed one (by page size) */
  MEM_EXTENDED_PARAMETER extended_params[2] = {};
  ULONG extended_params_count = 0;

  // First case first
  if constexpr (ExplicitPageSize) {
    if (!(os_info.page_sizes_mask & page_type)) [[unlikely]]
      return nullptr;  // Page size not supported

    auto& param = extended_params[0];
    param.Type = MemExtendedParameterAttributeFlags;
    param.ULong64 = page_type == *os_info.big_page_size
                    ? MEM_EXTENDED_PARAMETER_NONPAGED_LARGE
                    : MEM_EXTENDED_PARAMETER_NONPAGED_HUGE;
    ++extended_params_count;
  }

  // Now deal with the alignment requirement
  MEM_ADDRESS_REQUIREMENTS addr_reqs;

  // An innocent bit trick here: a pow2 is greater than the maximum of
  // two pow2's iff it's greater than their disjunction
  if (alignment > (min_size | os_info.granularity)) {
    addr_reqs = { .LowestStartingAddress = nullptr,
                  .HighestEndingAddress = nullptr,
                  .Alignment = alignment };

    auto& param = extended_params[extended_params_count++];
    param.Type = MemExtendedParameterAddressRequirements;
    param.Pointer = &addr_reqs;
  }

  // Also the basic param for both
  constexpr auto Flags =
    MEM_RESERVE | MEM_COMMIT | (RegularPages ? 0 : MEM_LARGE_PAGES);

  if (!extended_params_count) // Okay, regular alloc
    return VirtualAlloc(nullptr, size, Flags, PAGE_READWRITE);

  if (!os_info.extended_alloc)
    return nullptr;  // VirtualAlloc2 requested but not present

  return os_info.extended_alloc(nullptr, nullptr, size, Flags, PAGE_READWRITE,
                                extended_params, extended_params_count);
#else
  // Okay, this gonna be simpler for Unix-like systems, I guess...
  constexpr auto BaseFlags = MAP_PRIVATE | MAP_ANON;
  int extended_flags = 0;

  if constexpr (RegularPages) {
    // The only problem we might have with regular pages is the
    // alignment requirement...
    if (alignment > os_info.page_size) {
      // ... and we only know how to guarantee this on BSD, hehe
#  if MEMAW_IS(OS, BSD) && defined(MAP_ALIGNED)
      extended_flags|= MAP_ALIGNED(alignment.log2());
#  else
      return nullptr;
#  endif
    }
  }
  else if constexpr (ExplicitPageSize) {
    /* Not all systems support this. We'll deal with those that
     * do. But even on them we cannot guarantee alignment bigger than
     * the page size, so: */
    if (alignment > page_type) return nullptr;

#  if MEMAW_IS(OS, LINUX)
    extended_flags|= MAP_HUGETLB | (page_type.log2() << MAP_HUGE_SHIFT);
#  elif MEMAW_IS(OS, APPLE) && defined(VM_FLAGS_SUPERPAGE_SIZE_2MB)
    if (page_type != pow2_t{1 << 21, pow2_t::exact}) return nullptr;
    extended_flags|= VM_FLAGS_SUPERPAGE_SIZE_2MB;
#  else
    return nullptr; // Unsupported unfortunately
#  endif
  }
  else {
    /* Plain big pages flag is not always supported either. But we
     * won't return nullptr if it isn't here just in case, unless the
     * alignment requirement is too big */
#  if MEMAW_IS(OS, LINUX)
    if (os_info.big_page_size) {
      // Cannot guarantee big alignment on Linux
      if (alignment > *os_info.big_page_size) return nullptr;
    }
    else {
      /* This is also possible, unfortunately, because we could've
       * failed to read /proc/meminfo (e.g., if procfs was not mounted
       * for some paranoid reason). The problem with this is that now
       * we have no idea what alignment the page will be. All we can
       * assume is that it is at least 2 times the regular page
       * size. Since we're giving guarantees, we'll have to return
       * nullptr otherwise, I'm afraid */
      if (alignment > (os_info.page_size << 1)) return nullptr;
    }

    extended_flags|= MAP_HUGETLB;
#  else
    /* On other systems we have no idea what the page size will be (is
     * it even gonna be big, is our flag anything more than simply a
     * recommendation?). So we can't guarantee any alignment bigger
     * than the standard page size */
    if (alignment > os_info.page_size) return nullptr;

#    if MEMAW_IS(OS, APPLE) && defined(VM_FLAGS_SUPERPAGE_SIZE_ANY)
    extended_flags|= VM_FLAGS_SUPERPAGE_SIZE_ANY;
#    elif MEMAW_IS(OS, BSD) && defined(MAP_ALIGNED_SUPER)
    extended_flags|= MAP_ALIGNED_SUPER;
#    endif
#  endif
  }

  // That whole thing was only to get proper flags, can you imagine?
  // Now do the call itself
  void* const result =
    mmap(nullptr, size, PROT_READ | PROT_WRITE,
         BaseFlags | extended_flags, /*fd = */-1, /*offset = */0);

  if (result == MAP_FAILED) return nullptr;
  return result;

  // Simpler my ass...
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
