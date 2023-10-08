#pragma once
#include <gmock/gmock.h>
#include <utility>

#include "memaw/concepts.hpp"

using namespace memaw;

struct mock_resource {
  MOCK_METHOD(void*, allocate, (size_t, size_t));
  MOCK_METHOD(void, deallocate, (void*, size_t, size_t));
};

struct resource_params {
  bool nothrow_alloc = false;
  bool nothrow_dealloc = false;

  size_t min_size = 0;
  size_t alignment = 0;

  bool is_granular = false;
  bool is_sweeping = false;
  bool is_thread_safe = false;

  std::pair<int, int> group = { 0, 0 };
};

template <resource_params _params = resource_params{}, size_t _idx = 0>
struct test_resource {
  constexpr static bool is_granular = _params.is_granular;
  constexpr static bool is_sweeping = _params.is_sweeping;
  constexpr static bool is_thread_safe = _params.is_thread_safe;

  test_resource() = default;
  test_resource(mock_resource& _mock) noexcept: mock(&_mock) {}

  static size_t min_size() noexcept requires(_params.min_size > 0) {
    return _params.min_size;
  }

  static pow2_t guaranteed_alignment() noexcept
    requires(_params.alignment > 0) {
    return pow2_t{_params.alignment, pow2_t::exact};
  }

  void* allocate(const size_t size,
                 const size_t alignment = alignof(std::max_align_t))
    noexcept(_params.nothrow_alloc) {
    return mock->allocate(size, alignment);
  }

  void deallocate(void* const ptr, const size_t size,
                  const size_t alignment = alignof(std::max_align_t))
    noexcept(_params.nothrow_dealloc) {
    mock->deallocate(ptr, size, alignment);
  }

  bool operator==(const test_resource&) const noexcept = default;

  mock_resource* mock = nullptr;
};

template <resource_params _p1, size_t _i1, resource_params _p2, size_t _i2>
constexpr bool enable_interchangeable_resources<test_resource<_p1, _i1>,
                                                test_resource<_p2, _i2>> =
  _p1.group.first == _p2.group.first;

template <resource_params _p1, size_t _i1, resource_params _p2, size_t _i2>
constexpr bool enable_substitutable_resource_for<test_resource<_p1, _i1>,
                                                 test_resource<_p2, _i2>> =
  _p1.group.first < _p2.group.first && _p1.group.second <= _p2.group.second;
