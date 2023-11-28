#pragma once
#include "concepts.hpp"

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
};

} // namespace memaw
