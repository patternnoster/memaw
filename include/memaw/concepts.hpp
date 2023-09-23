#pragma once
#include <concepts>
#include <type_traits>

#include "__detail/base.hpp"
#include "__detail/concepts_impl.hpp"

/**
 * @file
 * Concepts for working with memory, used throughout this library
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

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

/**
 * @brief The concept of a resource that has a (constant) minimum
 *        allocation size limit. Such resources must define a static
 *        (but not necessarily constexpr) noexcept method min_size()
 *        that returns that minimum
 * @note  Semantic requirement: the return value of R::min_size() must
 *        never change (equality preservation) and be greater than 0
 **/
template <typename R>
concept bound_resource = resource<R> && requires() {
  { R::min_size() } noexcept -> std::convertible_to<size_t>;
};

/**
 * @brief A specializable global constant that enables the
 *        granular_resource concept (see below) for R. By default,
 *        true iff R defines a constexpr member R::is_granular
 *        implicitly convertible to true
 **/
template <bound_resource R>
constexpr bool enable_granular_resource = requires {
  { std::bool_constant<R::is_granular>{} } -> std::same_as<std::true_type>;
};

/**
 * @brief The concepts of a bound resource that can only allocate
 *        sizes that are multiples of its min_size()
 **/
template <typename R>
concept granular_resource = bound_resource<R> && enable_granular_resource<R>;

/**
 * @brief The concept of a resource that has a (constant) guaranteed
 *        alignment greater than alignof(std::max_align_t). Such
 *        resources must define a static (but not necessarily
 *        constexpr) method guaranteed_alignment() that returns that
 *        value
 * @note  Semantic requirements are obvious: the return value of
 *        R::guaranteed_alignment() must be a power of 2 greater than
 *        alignof(std::max_align_t) and must never change (equality
 *        preservation). Any pointers returned from an allocate() call
 *        to R must be aligned by (at least) that value
 **/
template <typename R>
concept overaligning_resource = resource<R> && requires() {
  { R::guaranteed_alignment() } noexcept -> std::same_as<pow2_t>;
};

/**
 * @brief A specializable global constant that enables the
 *        sweeping_resource concept (see below) for R. By default,
 *        true iff R defines a constexpr member R::is_sweeping
 *        implicitly convertible to true
 **/
template <resource R>
constexpr bool enable_sweeping_resource = requires {
  { std::bool_constant<R::is_sweeping>{} } -> std::same_as<std::true_type>;
};

/**
 * @brief The concept of a resource that permits combining
 *        deallocations of adjacent regions into one call
 *
 * More formally, r is a sweeping resource if and only if for any two
 * valid (see above) calls r.deallocate(ptr1, size1, alignment1) and
 * r.deallocate(ptr2, size2, alignment2), if (ptr2 == (byte*)ptr1 +
 * size1) then the call r.deallocate(ptr1, size1 + size2, alignment1)
 * is also valid and has the same effect as the two former calls (in
 * some order).
 *
 * To mark resource as sweeping one has to specialize the constant
 * enable_sweeping_resource to true. If then the above condition is
 * not satisfied, the behaviour of the program is undefined.
 *
 * @note If a resource never returns adjacent regions from allocate()
 *       (and is not interchangeable with any resource that does) then
 *       it is automatically sweeping (since no two deallocate() calls
 *       for adjacent regions are valid in the first place)
 **/
template <typename R>
concept sweeping_resource = resource<R> && enable_sweeping_resource<R>;

/**
 * @brief A specializable global constant that enables the
 *        thread_safe_resource concept (see below) for R. By default,
 *        true iff R defines a constexpr member R::is_thread_safe
 *        implicitly convertible to true
 **/
template <resource R>
constexpr bool enable_thread_safe_resource = requires {
  { std::bool_constant<R::is_thread_safe>{} } -> std::same_as<std::true_type>;
};

/**
 * @brief The concept of a resource whose methods can safely be called
 *        concurrently from different threads
 *
 * To mark resource as thread_safe one has to specialize the constant
 * enable_thread_safe_resource to true. If then the above condition is
 * not satisfied, the behaviour of the program is undefined.
 **/
template <typename R>
concept thread_safe_resource = resource<R> && enable_thread_safe_resource<R>;

/**
 * @brief The concept of a resource whose allocation, deallocation and
 *        equality testing methods don't throw exceptions. Such
 *        resources return nullptr whenever allocation fails. All
 *        resources in this library model this concept
 **/
template <typename R>
concept nothrow_resource = resource<R>
  && __detail::nothrow_equality_comparable<R>
  && requires(R res, void* ptr, size_t size, size_t alignment) {
  { res.allocate(size) } noexcept;
  { res.allocate(size, alignment) } noexcept;
  { res.deallocate(ptr, size) } noexcept;
  { res.deallocate(ptr, size, alignment) } noexcept;
};

} // namespace memaw
