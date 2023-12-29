#pragma once
#include <cstdint>
#include <set>

struct mock_resource;

using std::size_t;

/**
 * @brief A base for advanced resource tests providing proper mocks
 *        for real memory allocation
 **/
class resource_test_base {
protected:
  static void* align_by(void* const ptr, const size_t alignment) noexcept {
    return reinterpret_cast<void*>((uintptr_t(ptr) + (alignment - 1))
                                   & ~(alignment - 1));
  }

  void mock_deallocations(mock_resource&) noexcept;

  struct allocation {
    void* ptr;
    size_t size;
    size_t alignment;

    bool operator<(const allocation& rhs) const noexcept {
      return ptr < rhs.ptr;
    }
  };

  std::set<allocation> allocations;
};
