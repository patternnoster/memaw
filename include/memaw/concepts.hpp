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
 *   unless deallocate() corresponding (see below) to that call has
 *   been made;
 * - if alignment is a power of 2, allocate(size, alignment) must
 *   return a pointer aligned (at least) by alignment (meaning, ptr &
 *   (alignment - 1) == 0), whereas allocate(size) must return a pointer
 *   aligned by (at least) alignof(std::max_align_t);
 *
 * A call r1.deallocate(ptr, size, [alignment]) corresponds to a call
 * r2.allocate(size2, [alignment2])) if:
 * - ptr was returned by that allocate() call;
 * - r1 == r2;
 * - size == size2;
 * - alignment == alignment2, or not present in both calls.
 *
 * We say that a deallocate() call is valid if and only if it has an
 * allocate() call it corresponds to and no deallocation that
 * corresponds to the same call has happened. A valid deallocate()
 * call must succeed. Note that some resources might have less
 * restrictive conditions of when deallocate() and allocate()
 * correspond (e.g., the alignment parameter of deallocate() ignored).
 *
 * For every r.allocate() that returned non-null pointer, a
 * corresponding deallocate() call must be made before the destructor
 * of r is called. All the memory allocated by r must be released by
 * the time all destructors of instances that accepted those
 * deallocate() calls return.
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

/**
 * @brief A specializable global constant that enables the
 *        interchangeable_resource_with concept (see below) for R1 and
 *        R2 and vice versa. By default, true iff R1 defines a
 *        template constexpr member R1::is_interchangeable_with<R2>
 *        implicitly convertible to true
 **/
template <resource R1, resource R2>
constexpr bool enable_interchangeable_resources = requires {
  { std::bool_constant<R1::template is_interchangeable_with<R2>>{} }
    -> std::same_as<std::true_type>;
};

/**
 * @brief The concept of resources any two instances of which can
 *        safely deallocate memory allocated by the other.
 *
 * To mark resources as interchangeable one has to specialize the
 * constant enable_interchangeable_resources to true. If then the
 * above condition is not satisfied, the behaviour of the program is
 * undefined.
 *
 * Alternatively to mark a resource as interchangeable with itself
 * (meaning any two instances of it can process each other
 * deallocation calls) one can make its operator== and operator!=
 * return a type implicitly and constexpr-convertible to true and
 * false respectively (e.g., std::(true/false)_type).
 *
 * @note Interchangeability is not an equivalence relation: while
 *       symmetric, it is neither transitive nor even necessarily
 *       reflexive
 */
template <typename R1, typename R2>
concept interchangeable_resource_with = resource<R1> && resource<R2>
  && ((std::same_as<R1, R2> && __detail::equal_instances<R1>)
      || enable_interchangeable_resources<R1, R2>
      || enable_interchangeable_resources<R2, R1>);

/**
 * @brief A specializable global constant that enables the
 *        substitutable_resource_for concept (see below) for R1 and
 *        R2. By default, true iff R1 defines a template constexpr
 *        member R1::is_substitutable_for<R2> implicitly convertible
 *        to true
 **/
template <resource R1, resource R2>
constexpr bool enable_substitutable_resource_for = requires {
  { std::bool_constant<R1::template is_substitutable_for<R2>>{} }
    -> std::same_as<std::true_type>;
};

/**
 * @brief The concept of a resource that can safely accept all
 *        deallocation calls from a particular instance of another
 *        resource
 *
 * More formally, given instances r1 and r2, for every valid call
 * r2.deallocate(ptr, size, alignment) a call r1.deallocate(ptr, size,
 * alignment) is also valid, assuming no calls to r2.deallocate() has
 * been made, and all the memory is freed by the time both destructors
 * return.
 *
 * Obviously, if R1 and R2 are interchangeable, then this is always
 * true. If they are not then one can specialize the global constant
 * enable_substitutable_resource_for to make a resource substitutable
 * for another (in case the semantic requirement above is met)
 *
 * @note  This is a much weaker concept than interchangeable_with. It
 *        is not symmetric and only requires validity when all (and
 *        not some) of the deallocation calls from a particular
 *        instance of R2 are redirected to a particular instance of R1
 * @note  If R1 is sweeping, then it must accept mixed adjacent regions
 *        from both resources (if possible)
 **/
template <typename R1, typename R2>
concept substitutable_resource_for = resource<R1> && resource<R2>
  && (interchangeable_resource_with<R1, R2>
      || enable_substitutable_resource_for<R1, R2>);

} // namespace memaw
