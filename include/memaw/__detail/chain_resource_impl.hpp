#pragma once
#include <concepts>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "../concepts.hpp"

namespace memaw::__detail {

/**
 * @brief Gets the type at the specified position in a variadic list
 **/
template <size_t _idx, typename... Ts>
using at = std::tuple_element_t<_idx, std::tuple<Ts...>>;

// We will keep this undefined as it compiles much faster this way
template <typename...> struct type_list;

/**
 * @brief Finds the index of the element of the given list that models
 *        substitutable_resource_for all the other elements
 **/
template <typename, typename = type_list<>>
struct universal_deallocator_finder {
  constexpr static std::optional<size_t> index = {};
};

template <typename H, typename... Ts, typename... Us>
  requires((substitutable_resource_for<H, Ts> && ...)
           && ... && substitutable_resource_for<H, Us>)
struct universal_deallocator_finder<type_list<H, Ts...>, type_list<Us...>> {
  constexpr static std::optional<size_t> index = sizeof...(Us);
};

template <typename H, typename... Ts, typename... Us>
struct universal_deallocator_finder<type_list<H, Ts...>, type_list<Us...>> {
  constexpr static auto index =
    universal_deallocator_finder<type_list<Ts...>, type_list<Us..., H>>::index;
};

template <resource... Rs>
struct resource_list {
  constexpr static std::optional<size_t> universal_deallocator =
    universal_deallocator_finder<type_list<Rs...>>::index;

  constexpr static bool has_universal_deallocator =
    universal_deallocator.has_value();

  constexpr static size_t universal_deallocator_id =
    universal_deallocator.value_or(sizeof...(Rs));

  /**
   * @brief Returns the minimum value that is >= every min_size() of
   *        bound resources in the list and is a multiple of
   *        min_size() of granular resources in the list (if any)
   **/
  static auto get_min_size() noexcept requires(bound_resource<Rs> || ...) {
    return size_t(0);
  }

  /**
   * @brief Returns the minimum guaranteed_alignment() of all the
   *        resources in the list
   **/
  static pow2_t get_guaranteed_alignment() noexcept
    requires(overaligning_resource<Rs> && ...) {
    return {};
  }
};

template <typename C>
concept has_dispatcher = requires(C chain,
                                  void* ptr, size_t size, size_t alignment) {
  { dispatch_deallocate(chain, ptr, size, alignment) }
    -> std::convertible_to<size_t>;
};

template <typename C>
concept has_nothrow_dispatcher =
  requires(C chain, void* ptr, size_t size, size_t alignment) {
  { dispatch_deallocate(chain, ptr, size, alignment) } noexcept;
};

template <typename C>
using dispatcher_result_t =
  decltype(dispatch_deallocate(std::declval<C>(), nullptr, 0, 0));

template <typename C>
concept has_constant_dispatcher = has_dispatcher<C> && requires {
  typename std::integral_constant<size_t, dispatcher_result_t<C>::value>;
};

template <typename> constexpr size_t dispatch = {};

template <has_constant_dispatcher C>
constexpr size_t dispatch<C> = dispatcher_result_t<C>::value;

} // namespace memaw::__detail
