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

/**
 * @brief Memory resource that maintains lists of chunks of fixed
 *        sizes allocated from the upstream resource. Chunks are
 *        reused upon deallocation but are not returned to the
 *        upstream until this resource is destructed
 **/
template <sweeping_resource R,
          pool_resource_config _config = pool_resource_config{
            .thread_safe = thread_safe_resource<R>
          }>
class pool_resource {
public:
  static_assert(_config.min_chunk_size >= alignof(std::max_align_t));
  static_assert(_config.max_chunk_size % _config.min_chunk_size == 0);

  /**
   * @brief The underlying resource to allocate memory from
   **/
  using upstream_t = R;

  /**
   * @brief The configuration parameters of the resource
   **/
  constexpr static const pool_resource_config& config = _config;

  constexpr static bool is_granular = true;
  constexpr static bool is_sweeping = true;
  constexpr static bool is_thread_safe =
    _config.thread_safe && thread_safe_resource<upstream_t>;

  /**
   * @brief Returns the (configured) size of a minimum allocation: any
   *        allocation can only request a size that is a multiple of
   *        this value
   **/
  constexpr static pow2_t min_size() noexcept {
    return _config.min_chunk_size;
  }

  /**
   * @brief Returns the minimal alignment of any address allocated by
   *        the pool if its configuration allows that
   **/
  constexpr static pow2_t guaranteed_alignment() noexcept
    requires(_config.min_chunk_size > alignof(std::max_align_t)) {
    return _config.min_chunk_size;
  }
};

} // namespace memaw
