#pragma once
#include <cstdint>
#include <optional>

#include "base.hpp"
#include "environment.hpp"

#if MEMAW_IS(OS, WINDOWS)
#  include <windows.h>
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
  // Load the regular page size & granularity
#if MEMAW_IS(OS, WINDOWS)
  SYSTEM_INFO info;
  GetSystemInfo(&info);  // This call never fails

  page_size = pow2_t{info.dwPageSize, pow2_t::exact};
  granularity = pow2_t{info.dwAllocationGranularity, pow2_t::exact};
#else
  granularity = page_size = pow2_t{sysconf(_SC_PAGESIZE), pow2_t::exact};
#endif
}

} // namespace memaw::__detail
