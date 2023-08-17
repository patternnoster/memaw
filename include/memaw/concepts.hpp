#pragma once
#include <concepts>

#include "__detail/base.hpp"

namespace memaw {

/**
 * @brief The basic concept of a memory resource: can allocate and
 *        deallocate memory given size and alignment. All resources in
 *        the standard <memory_resource> and in this library model
 *        this concept.
 *
 * The semantic requirements are straightforward:
 * - if allocate(size) or allocate(size, alignment) returns a non-null
 *   pointer, the byte range [ptr, ptr + size) must be accessible and
 *   not intersect with any range returned from a previous call,
 *   unless deallocate() corresponding to that call has been made;
 * - if alignment is a power of 2, allocate(size, alignment) must
 *   return a pointer aligned (at least) by alignment (meaning, ptr &
 *   (alignment - 1) == 0), whereas allocate(size) must return a pointer
 *   aligned by (at least) alignof(std::max_align_t);
 *
 * If for every allocate() that returned non-null pointer, a
 * corresponding deallocate() call has been made to the same resource
 * instance, then all the memory used or returned by it must be
 * released by the time its destructor returns.
 **/
template <typename R>
concept resource = std::equality_comparable<R>
  && requires(R res, void* ptr, size_t size, size_t alignment) {
  { res.allocate(size) } -> std::same_as<void*>;
  { res.allocate(size, alignment) } -> std::same_as<void*>;
  res.deallocate(ptr, size);
  res.deallocate(ptr, size, alignment);
};

} // namespace memaw