#pragma once
#include <concepts>
#include <nupp/algorithm.hpp>
#include <type_traits>

#include "concepts.hpp"
#include "literals.hpp"

#include "__detail/cache_resource_impl.hpp"

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
   *        every allocation must be a multiple of this value. Must
   *        not be smaller than cache_resource::min_granularity (see
   *        below)
   * @note  This value also determines the guaranteed alignment and
   *        the alignment parameter passed to the upstream allocate()
   *        call
   **/
  const pow2_t granularity = pow2_t{4_KiB};

  /**
   * @brief The minimum (and also the initial) block size to allocate
   *        from the underlying resource. Must not be smaller than
   *        granularity
   * @note  If R is a bound_resource, any block size value will be
   *        automatically ceiled to be greater than R::min_size() (and
   *        additionally a multiple of it if R is also a
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

  /**
   * @brief Thread safety policy: if set to true, the implementation
   *        will use atomic instructions to manage its internal
   *        structures (requires DWCAS). The thread safe
   *        implementation is lock-free
   * @note  If the underlying resource is not thread safe, setting this
   *        parameter to true will change the type of instructions
   *        used but won't make the cache thread safe either
   **/
  const bool thread_safe = thread_safe_resource<upstream_resource>;
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

/**
 * @brief Memory resource that allocates big blocks from an upstream
 *        resource and uses those blocks for (smaller) allocation
 *        requests. Memory is not freed until the resource is
 *        destructed (as in std::pmr::monotonic_buffer_resource)
 **/
template <cache_resource_config auto _config>
class cache_resource {
public:
  /**
   * @brief The minimum value for the granularity parameter. Normally
   *        equals 32 bytes. Always >= alignof(std::max_align_t).
   **/
  constexpr static pow2_t min_granularity =
    __detail::cache_resource_impl<_config>::min_granularity;

  static_assert(_config.granularity >= min_granularity);
  static_assert(_config.min_block_size >= _config.granularity);
  static_assert(_config.max_block_size >= _config.min_block_size);
  static_assert(_config.max_block_size == _config.min_block_size
                || _config.block_size_multiplier > 1);

  using upstream_t = typename decltype(_config)::upstream_resource;

  constexpr static auto config = _config;

  constexpr static bool is_granular = true;
  constexpr static bool is_sweeping = true;
  constexpr static bool is_thread_safe =
    _config.thread_safe && thread_safe_resource<upstream_t>;

  template <resource R>
  constexpr static bool is_substitutable_for =
    substitutable_resource_for<upstream_t, R>;

  /**
   * @brief Returns the (configured) size of a minimum allocation: any
   *        allocation can only request a size that is a multiple of
   *        this value
   **/
  constexpr static pow2_t min_size() noexcept {
    return _config.granularity;
  }

  /**
   * @brief Returns the minimal alignment of any address allocated by
   *        the cache if its configuration allows that
   **/
  constexpr static pow2_t guaranteed_alignment() noexcept
    requires(_config.granularity > alignof(std::max_align_t)) {
    return _config.granularity;
  }

  constexpr cache_resource()
    noexcept(std::is_nothrow_default_constructible_v<upstream_t>)
    requires(std::default_initializable<upstream_t>) {}

  constexpr cache_resource(upstream_t&& upstream)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>):
    impl_(std::move(upstream)) {}

  cache_resource(const cache_resource&) = delete;
  cache_resource& operator=(const cache_resource&) = delete;

  constexpr cache_resource(cache_resource&& rhs)
    noexcept(std::is_nothrow_move_constructible_v<upstream_t>) = default;

  constexpr cache_resource& operator=(cache_resource&& rhs)
    noexcept(std::is_nothrow_move_assignable_v<upstream_t>)
    requires(std::is_move_assignable_v<upstream_t>) = default;

  /**
   * @brief Allocates memory from the cache, calling the upstream
   *        allocate() if there is not enough left
   * @param size must be a multiple of the configured granularity
   * @param alignment must be a power of 2. If it is greater than the
   *        configured granularity, additional padding will be used
   **/
  [[nodiscard]] void* allocate
    (const size_t size,
     const size_t alignment = alignof(std::max_align_t)) noexcept {
    return impl_.allocate(size, pow2_t{alignment, pow2_t::ceil});
  }

  /**
   * @brief Deallocates previously allocated memory. The deallocate()
   *        call on the upstream resource happens only on destruction
   **/
  void deallocate(void* const ptr, const size_t size,
                  const size_t alignment = alignof(std::max_align_t)) noexcept {
    impl_.deallocate(ptr, size, pow2_t{alignment, pow2_t::exact});
  }

  bool operator==(const cache_resource&) const noexcept = default;

private:
  __detail::cache_resource_impl<_config> impl_;
};

template <sweeping_resource R>
cache_resource(R&&) -> cache_resource<cache_resource_config_t<R>{}>;

} // namespace memaw
