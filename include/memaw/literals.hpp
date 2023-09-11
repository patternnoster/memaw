#pragma once

/**
 * @file
 * Convenient literals for byte sizes
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {

using ull = unsigned long long;  // Funny enough, on 64-bit systems
                                 // this is the same size but not the
                                 // same type as uint64_t
inline namespace common {

/* However ridiculous all those ibibytes and ebibytes sound, they are
 * now part of the official standard, adopted and recognized by IEC,
 * IEEE, NIST, ISO, and many other combinations of capital letters.
 * The same standard that defines "megabyte" to mean 10^6 bytes.
 * Unfair and outrageous, I know, but apparently disk manufacturers
 * with their typical need to make things appear bigger than they
 * really are have found more understanding amongst the members of
 * those committees than the basic common sense */
constexpr ull operator ""_KiB(const ull x) { return x << 10; }
constexpr ull operator ""_MiB(const ull x) { return x << 20; }
constexpr ull operator ""_GiB(const ull x) { return x << 30; }
constexpr ull operator ""_TiB(const ull x) { return x << 40; }
constexpr ull operator ""_PiB(const ull x) { return x << 50; }
constexpr ull operator ""_EiB(const ull x) { return x << 60; }

} // namespace common
} // namespace memaw
