#pragma once
#include "__detail/base.hpp"

namespace memaw {

/**
 * @brief Memory resource that always allocates and frees memory via
 *        direct system calls to the OS
 **/
class os_resource {
public:
  constexpr os_resource() noexcept = default;

  [[nodiscard]] static void* allocate
    (size_t size, size_t alignment = alignof(std::max_align_t)) noexcept;

  static void deallocate(void* ptr, size_t size,
                         size_t alignment = alignof(std::max_align_t)) noexcept;

  constexpr bool operator==(const os_resource&) const noexcept = default;
};

} // namespace memaw
