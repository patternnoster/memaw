#pragma once
#include "base.hpp"
#include "environment.hpp"

#if MEMAW_IS(OS, WINDOWS)
#  include <windows.h>
#else
#  include <unistd.h>
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

  size_t page_size;
};

inline static const os_info_t os_info{};

os_info_t::os_info_t() noexcept {
  // Load the regular page size
#if MEMAW_IS(OS, WINDOWS)
  SYSTEM_INFO info;
  GetSystemInfo(&info);  // This call never fails
  page_size = info.dwPageSize;
#else
  page_size = sysconf(_SC_PAGESIZE);
#endif
}

} // namespace memaw::__detail
