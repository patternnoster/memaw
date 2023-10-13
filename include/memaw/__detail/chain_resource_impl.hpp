#pragma once
#include "../concepts.hpp"

namespace memaw::__detail {

template <resource... Rs>
struct resource_list {
  constexpr static bool has_universal_deallocator = false;
  constexpr static size_t universal_deallocator_id = 0;

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

} // namespace memaw::__detail
