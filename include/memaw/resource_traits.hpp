#pragma once
#include "concepts.hpp"

#include "__detail/resource_traits_impl.hpp"

/**
 * @file
 * Unified interfaces to resource concepts and allocation methods
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {

/**
 * @brief A structure containing traits for memory resource concepts
 *        defined by this library
 **/
template <resource R>
struct resource_traits {
  /**
   * @brief True iff the resource has a minimal allocation size limit
   *        (see the bound_resource concept for details)
   **/
  constexpr static bool is_bound = bound_resource<R>;

  /**
   * @brief True iff the resource can only allocate multiples of its
   *        minimum allocation size (see the granular_resource concept
   *        for details)
   **/
  constexpr static bool is_granular = granular_resource<R>;

  /**
   * @brief True iff the resource can deallocate adjacent regions with
   *        a single call (see the sweeping_resource concept for
   *        details)
   **/
  constexpr static bool is_sweeping = sweeping_resource<R>;

  /**
   * @brief True iff the resource has a guaranteed alignment more than
   *        alignof(std::max_align_t) (see the overaligning_resource
   *        concept for details)
   **/
  constexpr static bool is_overaligning = overaligning_resource<R>;

  /**
   * @brief True iff the resource is thread safe (see the
   *        thread_safe_resource concept for details)
   **/
  constexpr static bool is_thread_safe = thread_safe_resource<R>;

  /**
   * @brief True iff the resource's methods don't throw exceptions
   *        (see the nothrow_resource concept for details)
   **/
  constexpr static bool is_nothrow = nothrow_resource<R>;

  /**
   * @brief True iff the resource and the provided one can always
   *        safely deallocate memory allocated by the other (see the
   *        interchangeable_resource_with concept for details)
   **/
  template <resource T>
  constexpr static bool is_interchangeable_with =
    interchangeable_resource_with<R, T>;

  /**
   * @brief True iff the resource can safely deallocate all memory
   *        allocated by any particalar instance of the provided one
   *        (see the substitutable_resource_for concept for details)
   **/
  template <resource T>
  constexpr static bool is_substitutable_for =
    substitutable_resource_for<R, T>;

  /**
   * @brief Gets the minimum allocation size limit for the resource
   **/
  constexpr static size_t min_size() noexcept {
    if constexpr (is_bound) return R::min_size();
    else return 0;
  }

  /**
   * @brief Returns the minimum allocation size that can be requested
   *        from the resource and is not less than the provided
   *        argument
   *
   * If R models granular_resource, returns the next (>=) multiple of
   * R::min_size(). If R is bound, returns R::min_size() if `size` is
   * less than it. Otherwise returns `size` unchanged.
   **/
  constexpr static size_t ceil_allocation_size(const size_t size) noexcept {
    return __detail::ceil_allocation_size_impl<R>(size);
  }

  /**
   * @brief Gets the minimum alignment of every allocation by the
   *        resource
   **/
  constexpr static pow2_t guaranteed_alignment() noexcept {
    if constexpr (is_overaligning) return R::guaranteed_alignment();
    else return pow2_t { alignof(std::max_align_t), pow2_t::exact };
  }
};

/**
 * @brief Specifies the requested exceptions policy for the global
 *        allocate()/deallocate() calls
 **/
enum class exceptions_policy {
  original,
  nothrow,
  throw_bad_alloc
};

/**
 * @brief Allocates memory from the given resource with the chosen
 *        exception policy. The parameters are forwarded to the
 *        R::allocate() call directly
 **/
template <exceptions_policy _policy = exceptions_policy::original,
          resource R>
[[nodiscard]] inline void* allocate
  (R& resource,
   const size_t size, const size_t alignment = alignof(std::max_align_t))
  noexcept(__detail::is_nothrow_with<_policy,
                                     __detail::has_nothrow_allocate<R>>) {
  return __detail::allocate_impl<_policy>(resource, size, alignment);
}

/**
 * @brief Deallocates memory to the given resource with the chosen
 *        exception policy. The parameters are forwarded to the
 *        R::deallocate() call directly
 **/
template <exceptions_policy _policy = exceptions_policy::original,
          resource R>
  requires (_policy != exceptions_policy::throw_bad_alloc)
inline void deallocate(R& resource, void* const ptr, const size_t size,
                       const size_t alignment = alignof(std::max_align_t))
  noexcept(__detail::is_nothrow_with<_policy,
                                     __detail::has_nothrow_deallocate<R>>) {
  __detail::deallocate_impl<_policy>(resource, ptr, size, alignment);
}

} // namespace memaw
