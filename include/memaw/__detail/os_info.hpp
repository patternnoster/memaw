#pragma once
#include <cstdint>
#include <optional>

#include "../literals.hpp"

#include "base.hpp"
#include "environment.hpp"

#if MEMAW_IS(OS, WINDOWS)
#  include <windows.h>
#  if MEMAW_IS(ARCH, X86_64)
#    include <intrin.h>  // for __cpuid
#  endif
#else
#  include <unistd.h>
#  if MEMAW_IS(OS, LINUX)
#    include <cstdio>
#    include <dirent.h>
#  endif
#endif

/**
 * @file
 * Getting runtime information about the OS
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

/**
 * @brief A class that gathers static information about the OS
 *        parameters and tools available in runtime
 **/
struct os_info_t {
public:
  inline os_info_t() noexcept;

  pow2_t page_size;
  std::optional<pow2_t> big_page_size;

  pow2_t granularity;

#if MEMAW_IS(OS, WINDOWS)
  decltype(&VirtualAlloc2) extended_alloc;
#endif

  uint64_t page_sizes_mask;

private:
  static inline std::optional<pow2_t> get_big_page_size() noexcept;
};

inline static const os_info_t os_info{};

std::optional<pow2_t> os_info_t::get_big_page_size() noexcept {
  uint64_t result = 0;

#if MEMAW_IS(OS, LINUX)
  /* On Linux we must parse /proc/meminfo (I wish there was a better
   * way...) And yes, we will assume procfs is mounted at /proc as it
   * is supposed to. The worst thing that could happen otherwise, we
   * will return an empty optional. Not great, not terrible */
  constexpr char MemInfoPath[] = "/proc/meminfo";

  char buf[255];
  if (const auto meminfo_fd = std::fopen(MemInfoPath, "r")) {
    while (std::fgets(buf, sizeof(buf), meminfo_fd)) {
      long unsigned value;
      /* This line is hardcoded into the kernel and is so unlikely to
       * change that we won't even bother asking "what if the size is
       * not in kB" */
      if (std::sscanf(buf, "Hugepagesize: %lu kB", &value) == 1) {
        result = uint64_t(value) << 10;  // in bytes
        break;
      }
    }
    std::fclose(meminfo_fd);
  }
#elif MEMAW_IS(OS, WINDOWS)
  // On Windows we can just request it through API though
  result = GetLargePageMinimum();
#endif

  if (!nupp::is_pow2(result)) return {}; // Empty optional
  return pow2_t{result, pow2_t::exact};
}

os_info_t::os_info_t() noexcept: big_page_size(get_big_page_size()) {
  // First load the regular page size & granularity
#if MEMAW_IS(OS, WINDOWS)
  SYSTEM_INFO info;
  GetSystemInfo(&info);  // This call never fails

  page_size = pow2_t{info.dwPageSize, pow2_t::exact};
  granularity = pow2_t{info.dwAllocationGranularity, pow2_t::exact};
#else
  granularity = page_size = pow2_t{sysconf(_SC_PAGESIZE), pow2_t::exact};
#endif

  // Now, we need to get the mask of all pages somehow
  page_sizes_mask = page_size;
  if (big_page_size) page_sizes_mask|= *big_page_size;

#if MEMAW_IS(OS, LINUX)
  /* On Linux we will honestly enlist the directories in sysfs (and
   * yes, we will once again assume it is properly mounted at /sys),
   * as there is no other way to get our list */
  constexpr char HugePagesPath[] = "/sys/kernel/mm/hugepages/";

  if (const auto hp_dir = opendir(HugePagesPath)) {
    while (const auto entry = readdir(hp_dir)) {
      long unsigned value;
      // The dirname is always like that (including kB) and is
      // unlikely to ever change, we assume
      if (std::sscanf(entry->d_name, "hugepages-%lukB", &value) == 1
          && nupp::is_pow2(value))
        page_sizes_mask|= value << 10;  // convert to bytes too
    }
    closedir(hp_dir);
  }
#elif MEMAW_IS(OS, WINDOWS)
  // On Windows, first check if the VirtualAlloc2 function is
  // available in runtime
  extended_alloc =
    (decltype(&::VirtualAlloc2))GetProcAddress(LoadLibrary("kernelbase.dll"),
                                               "VirtualAlloc2");

  /* Now, Windows normally decides for itself what big (large) pages
   * to use. But the extended alloc has this feature of asking for
   * HUGE (and not large) pages directly. We only know it to work on
   * x86_64 with the pdpe1gb CPU flag so far. So this is exactly what
   * we're going to check and when we're going to check it */
#  if MEMAW_IS(ARCH, X86_64)
  constexpr uint64_t mask_1gb = 1_GiB;
  if (extended_alloc && big_page_size && *big_page_size < mask_1gb) {
    constexpr int ExtendedFlag = 0x80000001;
    int cpu_data[4];  // EAX->EDX

    // First we check if the extended flag is available
    __cpuid(cpu_data, 0x80000000);
    if (cpu_data[0] >= ExtendedFlag) {
      // Now check for the pdpe1gb flag (bit 26 of EDX)
      __cpuid(cpu_data, ExtendedFlag);
      if (cpu_data[3] & (1 << 26)) page_sizes_mask|= mask_1gb;
    }
  }
#  endif
#endif
}

} // namespace memaw::__detail
