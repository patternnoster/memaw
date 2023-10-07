#pragma once
#include <concepts>
#include <type_traits>
#include <utility>

/**
 * @file
 * Implementation details of library concepts
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {
namespace __detail {

template <typename T>
concept nothrow_equality_comparable = std::equality_comparable<T>
  && requires(const std::remove_reference_t<T>& t) {
  {t == t} noexcept;
  {t != t} noexcept;
};

template <typename R>
concept has_nothrow_allocate = requires(R res, size_t size, size_t alignment) {
  { res.allocate(size) } noexcept;
  { res.allocate(size, alignment) } noexcept;
};

template <typename R>
concept has_nothrow_deallocate = requires(R res, void* ptr, size_t size,
                                          size_t alignment) {
  { res.deallocate(ptr, size) } noexcept;
  { res.deallocate(ptr, size, alignment) } noexcept;
};

template <typename T, typename... Ts>
concept same_as_either = (... || std::same_as<T, Ts>);

template <typename T>
concept equal_instances = std::equality_comparable<T>
  && requires(const std::remove_reference_t<T>& t) {
  { std::bool_constant<decltype(t == t){}>{} } -> std::same_as<std::true_type>;
  { std::bool_constant<decltype(t != t){}>{} } -> std::same_as<std::false_type>;
};

/**
 * @brief Enables the operators == and != returning std::true_type and
 *        std::false_type respectively for T. By default, true iff T
 *        defines a template constexpr member T::has_equal_instances
 *        implicitly convertible to true
 **/
template <typename T>
constexpr bool enable_equal_instances = requires {
  { std::bool_constant<T::has_equal_instances>{} }
    -> std::same_as<std::true_type>;
};

} // namespace __detail

inline namespace common {

template <typename T> requires(__detail::enable_equal_instances<T>)
constexpr std::true_type operator==(const T&, const T&) noexcept {
  return {};
}

template <typename T> requires(__detail::enable_equal_instances<T>)
constexpr std::false_type operator!=(const T&, const T&) noexcept {
  return {};
}

} // namespace common
} // namespace memaw
