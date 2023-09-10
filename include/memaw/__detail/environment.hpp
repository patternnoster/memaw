#pragma once

/**
 * @file
 * Macros to detect the compilation environment
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

// Detect the OS
#define __MEMAW__OS_LINUX    1
#define __MEMAW__OS_WINDOWS  2
#define __MEMAW__OS_APPLE    3  // MacOS and IOS
#define __MEMAW__OS_BSD      4
#define __MEMAW__OS_OTHER   -1

#if defined(__linux__) || defined(__linux)
#  define __MEMAW__OS __MEMAW__OS_LINUX
#elif defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
#  define __MEMAW__OS __MEMAW__OS_WINDOWS
#elif defined(__APPLE__) && defined(__MACH__)
#  define __MEMAW__OS __MEMAW__OS_APPLE
#elif defined(BSD) || defined(_SYSTYPE_BSD)
#  define __MEMAW__OS __MEMAW__OS_BSD
#else
#  define __MEMAW__OS __MEMAW__OS_OTHER
#endif

// Detect the architecture
#define __MEMAW__ARCH_X86_64  1
#define __MEMAW__ARCH_OTHER  -1  // don't need any other here :-)

#if defined(__x86_64) || defined(__x86_64__) || defined(_M_X64)
#  define __MEMAW__ARCH __MEMAW__ARCH_X86_64
#else
#  define __MEMAW__ARCH __MEMAW__ARCH_OTHER
#endif

// Environment macro function
#define MEMAW_IS(NAME, VALUE) \
  (__MEMAW__##NAME && __MEMAW__##NAME == __MEMAW__##NAME##_##VALUE)
