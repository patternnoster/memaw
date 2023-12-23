#pragma once
#include "concepts.hpp"
#include "literals.hpp"

/**
 * @file
 * A resource that allocates blocks of memory from the underlying
 * resource, breaks them into chunks of several fixed sizes and
 * maintains lists of those chunks to service (de-)allocation requests
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {

/**
 * @brief Configuration parameters for pool_resource with valid
 *        defaults
 **/
struct pool_resource_config {
  /**
   * @brief The minimum size of a chunk. The size of every allocation
   *        must be a multiple of this value (since every allocation
   *        must request full chunks).
   * @note  This value also determines the guaranteed alignment and
   *        the alignment parameter passed to the upstream allocate()
   *        call
   **/
  const pow2_t min_chunk_size = pow2_t{1_KiB};

  /**
   * @brief The maximum size of a chunk. Must equal min_chunk_size *
   *        chunk_size_multiplier^n for some n
   **/
  const size_t max_chunk_size = 16_KiB;

  /**
   * @brief The multiplier of chunk sizes. Determines the number of
   *        chunk lists maintained and the size of allocation from the
   *        upstream (which is max_chunk_size * chunk_size_multiplier
   *        normally)
   **/
  const size_t chunk_size_multiplier = 2;

  /**
   * @brief Thread safety policy: if set to true, the implementation
   *        will use atomic instructions to manage its internal
   *        structures (requires DWCAS). The thread safe
   *        implementation is lock-free
   * @note  If the underlying resource is not thread safe, setting this
   *        parameter to true will change the type of instructions
   *        used but won't make the pool thread safe either
   **/
  const bool thread_safe = true;
};

} // namespace memaw
