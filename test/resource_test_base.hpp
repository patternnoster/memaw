#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
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

  static bool has_intersections(const std::set<allocation>& allocs) noexcept {
    if (allocs.empty()) return false;

    for (auto it = allocs.begin(), next_it = std::next(it);
         next_it != allocs.end(); it = next_it, ++next_it)
      if (uintptr_t(next_it->ptr) < (uintptr_t(it->ptr) + it->size))
        return true;

    return false;
  }

  std::set<allocation> allocations;
};

/**
 * @brief A base for advanced single threaded resource tests
 **/
class resource_test: public resource_test_base {
protected:
  struct allocation_request {
    size_t size;
    size_t alignment;
  };

  void mock_allocations(mock_resource&, std::deque<allocation_request>&&,
                        const size_t min_align = alignof(std::max_align_t));

  std::byte* get_next_ptr() const noexcept {
    return next_ptr_;
  }

private:
  std::deque<allocation_request> requests_;

  std::unique_ptr<std::byte[]> memory_;
  std::byte* next_ptr_ = nullptr;
};

/**
 * @brief A base for advanced multithreaded resource tests
 **/
class resource_multithreaded_test: public resource_test_base {
protected:
  void mock_allocations(mock_resource&, const size_t count,
                        const size_t max_size, const size_t min_align);

  void mock_deallocations(mock_resource&);

  /**
   * @brief Makes sure every allocation in the provided set is a part
   *        of some block
   **/
  void verify_allocations(const std::set<allocation>&) noexcept;

private:
  std::unique_ptr<std::byte[]> memory_;
  std::atomic<std::byte*> next_ptr_;

  std::unique_ptr<allocation[]> blocks_;
  std::atomic<size_t> last_block_;
};
