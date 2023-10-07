#pragma once
#include <concepts>
#include <tuple>
#include <type_traits>

#include "concepts.hpp"

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
 **/
template <resource... Rs> requires(sizeof...(Rs) > 0)
class chain_resource {
public:
  constexpr chain_resource()
    noexcept((std::is_nothrow_default_constructible_v<Rs> && ...))
    requires((std::default_initializable<Rs> && ...)) {}

  constexpr chain_resource(Rs&&... resources)
    noexcept((std::is_nothrow_move_constructible_v<Rs> && ...))
    requires((std::move_constructible<Rs> && ...))
    : resources_(std::move(resources)...) {}

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
   **/
  [[nodiscard]] std::pair<void*, size_t> do_allocate
    (const size_t size,
     const size_t alignment = alignof(std::max_align_t)) noexcept {
    return {};
  }

  /**
   * @brief Deallocates memory previously allocated by the chain by
   *        forwarding the call to one of the resources in it
   **/
  void deallocate(void* const ptr, const size_t size,
                  const size_t alignment = alignof(std::max_align_t)) noexcept {
  }

  /**
   * @brief Deallocates memory previously allocated by the chain by
   *        forwarding the call to the resource at the given index
   **/
  void deallocate_with(const size_t idx, void* const ptr, const size_t size,
                       const size_t alignment = alignof(std::max_align_t))
    noexcept((__detail::has_nothrow_deallocate<Rs> && ...)) {
  }

  constexpr bool operator==(const chain_resource&) const
    noexcept((__detail::nothrow_equality_comparable<Rs> && ...)) = default;

private:
  std::tuple<Rs...> resources_;
};

} // namespace memaw
