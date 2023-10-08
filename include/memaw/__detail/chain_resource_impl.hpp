#pragma once
#include "../concepts.hpp"

namespace memaw::__detail {

template <resource... Rs>
struct resource_list {
  constexpr static bool has_universal_deallocator = false;
  constexpr static size_t universal_deallocator_id = 0;
};

} // namespace memaw::__detail
