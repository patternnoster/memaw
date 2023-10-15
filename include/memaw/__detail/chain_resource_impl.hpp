#pragma once
#include <concepts>
#include <nupp/algorithm.hpp>
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

namespace tl {

/* Some meta tricks with type_list we're going to use.
 * We'll allow ourselves to have the default implementation inside the
 * definition: this is a __detail header anyway, only the wisest ones
 * will read it, they can figure this out */

/**
 * @brief Specifies if a type_list is empty
 **/
template <typename> constexpr bool empty = true;

/**
 * @brief Joins two type lists into one
 **/
template <typename, typename> struct join;

/**
 * @brief Breaks the type_list into two lists: those types that
 *        satisfy the given predicate and those that don't
 **/
template <typename, template <typename> typename>
struct partition {
  template <bool> using result = type_list<>;
};

/**
 * @brief Removes the types that don't satisfy the given predicate
 *        from the type_list
 **/
template <typename L, template <typename> typename Pred>
using filter_r = partition<L, Pred>::template result<true>;

/**
 * @brief Removes all duplicates (using std::same_as) from the
 *        type_list
 **/
template <typename>
struct unique {
  using result = type_list<>;
};

/**
 * @brief A functor that calls the templated operator() of F with the
 *        types of the given list
 **/
template <typename> struct applicator;
template <typename L> constexpr applicator<L> apply{};

/* Now the implementations... */

template <typename H, typename... Ts>
constexpr bool empty<type_list<H, Ts...>> = false;

template <typename... Ts, typename... Us>
struct join<type_list<Ts...>, type_list<Us...>> {
  using result = type_list<Ts..., Us...>;
};

template <typename H, typename... Ts, template <typename> typename Pred>
struct partition<type_list<H, Ts...>, Pred> {
  using next_partition = partition<type_list<Ts...>, Pred>;

  template <bool _b>
  using result =
    join<std::conditional_t<(Pred<H>::value == _b), type_list<H>, type_list<>>,
         typename partition<type_list<Ts...>,
                            Pred>::template result<_b>>::result;
};

template <typename H, typename... Ts>
struct unique<type_list<H, Ts...>> {
  template <typename T>
  using not_same_as_head = std::bool_constant<!std::same_as<H, T>>;

  using result =
    join<type_list<H>,
         typename unique<filter_r<type_list<Ts...>,
                                  not_same_as_head>>::result>::result;
};

template <typename... Ts>
struct applicator<type_list<Ts...>> {
  constexpr applicator() noexcept = default;

  template <typename F>
  decltype(auto) operator()(F&& func) const noexcept {
    return std::forward<F>(func).template operator()<Ts...>();
  }
};

} // namespace tl

template <resource R>
using is_bound = std::bool_constant<bound_resource<R>>;

template <resource R>
using is_granular = std::bool_constant<granular_resource<R>>;

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
  using unique_list = tl::unique<type_list<Rs...>>::result;

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
    using granular_partition =
      tl::partition<tl::filter_r<unique_list, is_bound>, is_granular>;

    using granular = granular_partition::template result<true>;
    using bound_non_granular = granular_partition::template result<false>;

    if constexpr (tl::empty<bound_non_granular>)
      return tl::apply<granular>([]<typename... Ts> {
          return nupp::lcm(Ts::min_size()...);
        });
    else {
      // Small trick here: since lcm is always >= than its arguments,
      // we only need to calculate maximum for non-granular resources
      const auto non_granular_max =
        tl::apply<bound_non_granular>([]<typename... Ts> {
            return nupp::maximum(Ts::min_size()...);
          });

      if constexpr (tl::empty<granular>) return non_granular_max;
      else
        return tl::apply<granular>([non_granular_max]<typename... Ts> {
            return nupp::lcm(non_granular_max, Ts::min_size()...);
          });
    }
  }

  /**
   * @brief Returns the minimum guaranteed_alignment() of all the
   *        resources in the list
   **/
  static pow2_t get_guaranteed_alignment() noexcept
    requires(overaligning_resource<Rs> && ...) {
    return tl::apply<unique_list>([]<typename... Ts> {
        return nupp::minimum(Ts::guaranteed_alignment()...);
      });
  }
};

template <resource R>
inline void* try_allocate(R& resource, const size_t size,
                          const size_t alignment) noexcept {
  if constexpr (has_nothrow_allocate<R>)
    return resource.allocate(size, alignment);
  else
    try { return resource.allocate(size, alignment); }
    catch (...) { return nullptr; }
}

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
