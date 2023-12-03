#pragma once
#include <concepts>
#include <new>
#include <nupp/algorithm.hpp>

#include "../concepts.hpp"

/**
 * @file
 * Implementation details of resource traits
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw::__detail {

/**
 * @brief Returns the next smallest allocation size that is valid for
 *        resource R (i.e., is not less than its min_size(), if it's
 *        bound, and a multiple of it if it's also granular)
 **/
template <resource R>
constexpr static size_t ceil_allocation_size_impl(const size_t size) noexcept {
  if constexpr (granular_resource<R>) {
    const auto min_size = R::min_size();
    const auto remainder = size % min_size;
    return size + (remainder > 0) * (min_size - remainder);
  }
  else if constexpr (bound_resource<R>)
    return nupp::maximum(size, R::min_size());
  else return size;
}

#ifdef __cpp_exceptions
template <typename R, typename... Args>
inline void* try_allocate(R& resource, const Args... args) noexcept
  requires (!has_nothrow_allocate<R>) try {
    return resource.allocate(args...);
  } catch (...) { return nullptr; }

template <typename R, typename... Args>
inline void try_deallocate(R& resource, const Args... args) noexcept
  requires (!has_nothrow_deallocate<R>) try {
    resource.deallocate(args...);
  } catch (...) {}
#endif

template <typename R, typename... Args>
inline void* try_allocate(R& resource, const Args... args) noexcept {
  return resource.allocate(args...);
}

template <typename R, typename... Args>
inline void try_deallocate(R& resource, const Args... args) noexcept {
  resource.deallocate(args...);
}

template <auto _policy, bool _original>
constexpr static bool is_nothrow_with = _policy == decltype(_policy)::nothrow
  || (_policy == decltype(_policy)::original && _original);

template <auto _policy, resource R>
[[nodiscard]] inline void* allocate_impl(R& resource, const size_t size,
                                         const size_t alignment)
  noexcept(is_nothrow_with<_policy, has_nothrow_allocate<R>>) {
  using policy_t = decltype(_policy);

  if constexpr (_policy == policy_t::nothrow)
    return try_allocate(resource, size, alignment);
  else {
    const auto result = resource.allocate(size, alignment);
#ifdef __cpp_exceptions
    if constexpr (_policy == policy_t::throw_bad_alloc)
      if (!result)
        throw std::bad_alloc{};
#else
    static_assert(_policy == exception_policy::throw_bad_alloc,
                  "The force exceptions policy has been selected "
                  "but exceptions are disabled");
#endif
    return result;
  }
}

template <auto _policy, resource R>
inline void deallocate_impl(R& resource, void* const ptr,
                            const size_t size, const size_t alignment)
  noexcept(is_nothrow_with<_policy, has_nothrow_deallocate<R>>) {
  if constexpr (_policy == decltype(_policy)::nothrow)
    try_deallocate(resource, ptr, size, alignment);
  else
    resource.deallocate(ptr, size, alignment);
}

} // namespace memaw::__detail
