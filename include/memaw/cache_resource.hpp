#pragma once
#include <concepts>

#include "concepts.hpp"
#include "literals.hpp"

/**
 * @file
 * A resource adaptor that allocates big blocks of memory from the
 * underlying resource upfront and then uses those blocks to service
 * allocation requests
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {

/**
 * @brief Configuration parameters for cache_resource with valid
 *        defaults
 **/
template <sweeping_resource R>
struct cache_resource_config_t {
  /**
   * @brief The underlying resource to cache (a template parameter)
   **/
  using upstream_resource = R;

  /**
   * @brief The allocation granularity for this adaptor. The size of
   *        every allocation must be a multiple of this value
   * @note  If R is an overaligning_resource, the cache will model
   *        overaligning_resource as well with the guaranteed
   *        alignment value equal to the minimum of that of R and this
   *        value
   **/
  const pow2_t granularity = pow2_t{4_KiB};

  /**
   * @brief The minimum (and also the initial) block size to allocate
   *        from the underlying resource. Must not be smaller than
   *        granularity
   * @note  If R is a bound_resource, any block size value will be
   *        automatically adjusted to be greater than R::min_size()
   *        (and additionally a multiple of it if R is also a
   *        granular_resource) at runtime
   **/
  const size_t min_block_size = 32_MiB;

  /**
   * @brief The maximum block size to allocate from the underlying
   *        resource (must not be smaller than min_block_size)
   * @note  If an allocation fails at some point, a previous (smaller
   *        by block_size_multiplier to some power, but not less than
   *        min_block_size) size will be tried if possible. That will
   *        require additional R::allocate() calls
   **/
  const size_t max_block_size = 1_GiB;

  /**
   * @brief The multiplier for every next block allocation from the
   *        underlying resource (until max_block_size is reached),
   *        must be greater than 1, unless the min_ and max_block_size
   *        are equal (in which case this value is ignored)
   * @note  Sequential increase is intended but not guaranteed. At any
   *        point the implementation may choose to allocate a block of
   *        any size min_block_size * block_size_multiplier^n for some
   *        integral n >= 0, but not greater than max_block_size
   **/
  const double block_size_multiplier = 2.0;
};

/**
 * @brief The concept of a set of configuration parameters for
 *        cache_resource
 **/
template <typename C>
concept cache_resource_config = requires {
  { cache_resource_config_t<typename C::upstream_resource>{} }
    -> std::same_as<C>;
};

} // namespace memaw
