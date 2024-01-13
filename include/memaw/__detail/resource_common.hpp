#pragma once
#include <concepts>
#include <utility>

#include "base.hpp"

/**
 * @file
 * Some common detail structures that come in handy in resource
 * implementations
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

/**
 * @brief Aligns the given pointer (up) by the given alignment and
 *        returns the new pointer value and the number of bytes used
 *        as padding
 **/
constexpr std::pair<uintptr_t, size_t> align_pointer
  (const auto ptr, const pow2_t alignment) noexcept {
  const auto ptr_uint = uintptr_t(ptr);

  const auto result = (ptr_uint + alignment.get_mask()) & ~alignment.get_mask();
  const auto padding = result - ptr_uint;
  return std::make_pair(result, padding);
}

/**
 * @brief The concept of types used to mark free memory chunks
 **/
template <typename C>
concept free_chunk = requires(C chunk) {
  { chunk.next } -> std::convertible_to<C*>;
  { chunk.size } -> std::convertible_to<size_t>;
};

/**
 * @brief Takes a (singly) linked list of memory chunks and merges
 *        consecutive chunks together (increasing the size value if
 *        necessary). Returns the head of the new list.
 **/
template <free_chunk C>
inline C* merge_chunks(C* const head) noexcept {
  C* turkish_delight = nullptr;  // "result", but I couldn't resist

  for (auto chunk = head; chunk;) {
    const auto chunk_begin = uintptr_t(chunk);
    const auto chunk_end = chunk_begin + chunk->size;

    const auto next_chunk = chunk->next;

    // This works like insertion sort, only we merge the adjacent
    // regions together
    C* prev_region = nullptr;          // null or < chunk_begin
    C* next_region = turkish_delight;  // null or > chunk_begin
    while (next_region && chunk_begin > uintptr_t(next_region)) {
      prev_region = next_region;
      next_region = next_region->next;
    }

    // We either put chunk between prev and next or merge 2 or 3 of
    // them together

    if (uintptr_t(next_region) == chunk_end) {
      // Merge with next (never 0 here)
      chunk->size+= next_region->size;
      chunk->next = next_region->next;
      next_region->~C();
    }
    else chunk->next = next_region;

    if (prev_region) {
      const auto prev_region_end = uintptr_t(prev_region) + prev_region->size;

      if (prev_region_end == chunk_begin) {
        // Merge with prev
        prev_region->size+= chunk->size;
        prev_region->next = chunk->next;
        chunk->~C();
      }
      else prev_region->next = chunk;
    }
    else turkish_delight = chunk;  // Put to the front

    chunk = next_chunk;
  }

  return turkish_delight/* on a moonlit night!*/;
}

} // namespace memaw::__detail
