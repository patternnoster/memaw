#pragma once
#include <nupp/pow2_t.hpp>
#include <type_traits>

#include "os_resource.hpp"

/**
 * @file
 * A few convenience aliases and wrappers around os_resource that
 * allocate different types of full pages of memory
 *
 * @author    patternnoster@github
 * @copyright 2023, under the MIT License (see /LICENSE for details)
 **/

namespace memaw {

/**
 * @brief A wrapper around os_resource, allocating memory directly
 *        from the OS using pages of the statically specified size
 **/
template <page_type auto _type>
class pages_resource {
public:
  using page_type_t = std::remove_const_t<decltype(_type)>;

  constexpr static bool is_granular = true;
  constexpr static bool is_sweeping = true;
  constexpr static bool is_thread_safe = true;

  constexpr static bool explicit_size = std::same_as<page_type_t, pow2_t>;

  constexpr pages_resource() noexcept = default;

  /**
   * @brief Get the minimum allocation size for this resource
   **/
  constexpr static pow2_t min_size() noexcept requires(explicit_size) {
    return _type;
  }

  static pow2_t min_size() noexcept requires(!explicit_size) {
    return os_resource::min_size(_type);
  }

  /**
   * @brief Get the minimum alignment every allocated address has
   **/
  constexpr static pow2_t guaranteed_alignment() noexcept
    requires(explicit_size) {
    return _type;
  }

  static pow2_t guaranteed_alignment() noexcept requires(!explicit_size) {
    return os_resource::guaranteed_alignment(_type);
  }

  /**
   * @brief Allocate a region of memory of the given size and
   *        alignment with a direct syscall (see
   *        os_resource::allocate() for details)
   **/
  [[nodiscard]] static void* allocate
    (const size_t size,
     const size_t alignment = alignof(std::max_align_t)) noexcept {
    return os_resource::allocate(size, alignment, _type);
  }

  /**
   * @brief Deallocate a previously allocated region of memory (see
   *        os_resource::deallocate() for details)
   **/
  static void deallocate(void* const ptr, const size_t size,
                         const size_t /*alignment, ignored */= 1) noexcept {
    os_resource::deallocate(ptr, size);
  }

  constexpr bool operator==(const pages_resource&) const noexcept = default;
};

/**
 * @brief A resource that allocates pages of regular system size
 *        directly from the OS
 **/
using regular_pages_resource = pages_resource<page_types::regular>;

/**
 * @brief A resource that allocates big (huge, large, super-) pages
 *        directly from the OS using system defaults
 **/
using big_pages_resource = pages_resource<page_types::big>;

/**
 * @brief A resource that allocates pages of the given fixed size
 *        directly from the OS
 **/
template <size_t _size> requires(nupp::is_pow2(_size))
using fixed_pages_resource = pages_resource<pow2_t(_size, pow2_t::exact)>;

} // namespace memaw
