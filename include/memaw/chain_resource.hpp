#pragma once
#include <concepts>
#include <tuple>
#include <type_traits>
#include <utility>

#include "concepts.hpp"
#include "resource_traits.hpp"

#include "__detail/chain_resource_impl.hpp"

/**
 * @file
 * A resource adaptor that combines several resources in a chain
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {

/**
 * @brief  Memory resource adaptor that sequentially tries every
 *         resource in a given list until a successful allocation
 *
 * The deallocate() call to the chain will, by default, be forwarded
 * to the first resource in the list that is substitutable_for all the
 * other resources. The dispatch_deallocate() template (see below) can
 * be specialized to alter that behaviour.
 *
 * If no such resource and no specialization of the dispatch template
 * exists, the default deallocate() method will be unavailable (thus,
 * the chain won't model the resource concept) and one should use the
 * deallocate_with() method (that takes an additional index argument)
 * instead
 **/
template <resource... Rs> requires(sizeof...(Rs) > 0)
class chain_resource {
public:
  /**
   * @brief Specifies if the chain is granular (i.e., at least one of
   *        its resources is granular)
   **/
  constexpr static bool is_granular = (granular_resource<Rs> || ...);

  /**
   * @brief Specifies if the chain is thread_safe (i.e., all of its
   *        resources are thread_safe)
   **/
  constexpr static bool is_thread_safe = (thread_safe_resource<Rs> && ...);

  /**
   * @brief Specifies if the chain is interchangeable with the given
   *        resource (i.e., all of its resources are interchangeable
   *        with it)
   **/
  template <resource R>
  constexpr static bool is_interchangeable_with =
    (interchangeable_resource_with<R, Rs> && ...);

  /**
   * @brief Specifies if all the instances of the chain are equal
   *        (i.e., all of its resources have equal_instances)
   **/
  constexpr static bool has_equal_instances =
    (__detail::equal_instances<Rs> && ...);

  constexpr chain_resource()
    noexcept((std::is_nothrow_default_constructible_v<Rs> && ...))
    requires((std::default_initializable<Rs> && ...)) {}

  constexpr chain_resource(Rs&&... resources)
    noexcept((std::is_nothrow_move_constructible_v<Rs> && ...))
    requires((std::move_constructible<Rs> && ...))
    : resources_(std::move(resources)...) {}

  /**
   * @brief Returns the minimum allocation size, defined iff any of
   *        the resources in the chain are bound.
   *
   * If defined, equals the smallest number n such that:
   * - for any bound_resource R in the chain, n >= R::min_size();
   * - for any granular_resource R in the chain, n is also a multiple
   *   of R::min_size()
   *
   * @note  If such n is not representable as size_t, the behaviour is
   *        undefined
   **/
  constexpr static size_t min_size() noexcept
    requires(bound_resource<Rs> || ...) {
    return __detail::resource_list<Rs...>::get_min_size();
  }

  /**
   * @brief Returns the guaranteed alignment of all the memory
   *        addresses allocated by the chain, defined iff all of the
   *        resources in the chain are overaligning (and equals the
   *        minimum of all their guaranteed_alignment()s)
   **/
  constexpr static pow2_t guaranteed_alignment() noexcept
    requires(overaligning_resource<Rs> && ...) {
    return __detail::resource_list<Rs...>::get_guaranteed_alignment();
  }

  /**
   * @brief Calls allocate() with the given size and alignment
   *        arguments on every resource in the list until the first
   *        success (i.e., a call that returned a non-null pointer)
   *        and returns that result. Returns nullptr if all of the
   *        calls have failed
   **/
  [[nodiscard]] void* allocate
    (const size_t size,
     const size_t alignment = alignof(std::max_align_t)) noexcept {
    return do_allocate(size, alignment).first;
  }

  /**
   * @brief Same as allocate() but returns a pair of the resulting
   *        pointer and the index of the resource that performed the
   *        allocation
   * @note  If the resulting pointer is null (i.e., if all the
   *        resources have been tried and have failed), the index of
   *        the last resource is always returned, even if that call
   *        threw an exception
   **/
  [[nodiscard]] std::pair<void*, size_t> do_allocate
    (const size_t size,
     const size_t alignment = alignof(std::max_align_t)) noexcept {
    return std::apply([size, alignment](Rs&... resources) {
      std::pair<void*, size_t> result{ nullptr, size_t(-1) };
      ((++result.second,
        result.first = memaw::allocate<exceptions_policy::nothrow>
          (resources, size, alignment)) || ...);
      return result;
    }, resources_);
  }

  /**
   * @brief Chooses the resource in the chain that will service the
   *        deallocate() call
   *
   * By default, only declared for chains that have a resource that is
   * substitutable_for all the other resources (in which case returns
   * the index of the first such resource). Can be overloaded,
   * returning any type convertible to size_t.
   *
   * If the index is constant and known at compile-time (as in the
   * default scenario), std::integral_constant (or a similar type)
   * should be returned as it allows to deduce some compile-time
   * features (e.g., if the chain models substitutable_for or if the
   * deallocation is noexcept in case that's not true for all the
   * resources in the chain), and, additionally, can lead to
   * generating more efficient code on some compilers (e.g., MSVC)
   **/
  template <resource... Us>
    requires(__detail::resource_list<Us...>::has_universal_deallocator)
  friend std::integral_constant
    <size_t, __detail::resource_list<Us...>::universal_deallocator_id>
    dispatch_deallocate(const chain_resource<Us...>&, void*, size_t, size_t);

  /**
   * @brief Deallocates memory previously allocated by the chain by
   *        forwarding the call to one of the resources in it, using
   *        the dispatch_deallocate() (see above) function to get its
   *        index, and is only defined if such method is declared for
   *        the chain
   **/
  void deallocate(void* const ptr, const size_t size,
                  const size_t alignment = alignof(std::max_align_t))
    noexcept((__detail::has_constant_dispatcher<chain_resource>
              && __detail::has_nothrow_deallocate
                   <__detail::at<__detail::dispatch<chain_resource>, Rs...>>)
             || (__detail::has_nothrow_dispatcher<chain_resource>
                 && ... && __detail::has_nothrow_deallocate<Rs>))
    requires(__detail::has_dispatcher<chain_resource>) {
    if constexpr (__detail::has_constant_dispatcher<chain_resource>)
      std::get<__detail::dispatch<chain_resource>>(resources_)
        .deallocate(ptr, size, alignment);
    else
      deallocate_with(dispatch_deallocate(*this, ptr, size, alignment),
                      ptr, size, alignment);
  }

  /**
   * @brief Deallocates memory previously allocated by the chain by
   *        forwarding the call to the resource at the given index
   **/
  void deallocate_with(size_t idx, void* const ptr, const size_t size,
                       const size_t alignment = alignof(std::max_align_t))
    noexcept((__detail::has_nothrow_deallocate<Rs> && ...)) {
    std::apply([&idx, ptr, size, alignment](Rs&... resources) {
      (void)((!idx--
              && (resources.deallocate(ptr, size, alignment), true)) || ...);
    }, resources_);
  }

  constexpr bool operator==(const chain_resource&) const
    noexcept((__detail::nothrow_equality_comparable<Rs> && ...))
    requires(!has_equal_instances) = default;

private:
  std::tuple<Rs...> resources_;
};

/**
 * @brief Enables the sweeping_resource concept for chains with the
 *        constant deallocator if the corresponding resource is sweeping
 **/
template <resource... Rs>
  requires(__detail::has_constant_dispatcher<chain_resource<Rs...>>)
constexpr bool enable_sweeping_resource<chain_resource<Rs...>> =
  sweeping_resource<__detail::at<__detail::dispatch<chain_resource<Rs...>>,
                                 Rs...>>;

/**
 * @brief Specifies if a resource is substitutable for a chain (i.e.,
 *        if it is substitutable for all of its resources)
 **/
template <resource R, resource... Rs>
constexpr bool enable_substitutable_resource_for<R, chain_resource<Rs...>> =
  (substitutable_resource_for<R, Rs> && ...);

/**
 * @brief Enables the substitutable_resource_for concept for chains
 *        with the constant deallocator if the corresponding resource
 *        is substitutable for the given one
 **/
template <resource... Rs, resource R>
  requires(__detail::has_constant_dispatcher<chain_resource<Rs...>>
           && !requires(R r) { []<class... Ts>(chain_resource<Ts...>&){}(r); })
constexpr bool enable_substitutable_resource_for<chain_resource<Rs...>, R> =
  substitutable_resource_for
    <__detail::at<__detail::dispatch<chain_resource<Rs...>>, Rs...>, R>;

} // namespace memaw
