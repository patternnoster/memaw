#include "resource_test_base.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "test_resource.hpp"

using testing::_;
using testing::AtMost;

void resource_test_base::mock_deallocations(mock_resource& mock) noexcept {
  EXPECT_CALL(mock, deallocate(_, _, _))
    .Times(AtMost(int(allocations.size())))
    .WillRepeatedly([this](void *ptr, size_t size, const size_t alignment) {
      const auto it = allocations.find({ptr, size, alignment});

      ASSERT_NE(it, allocations.end());
      ASSERT_LE(it->size, size);
      EXPECT_EQ(it->alignment, alignment);

      // Allow sweeping
      size -= it->size;
      auto last_it = std::next(it);
      while (size) {
        ASSERT_NE(last_it, allocations.end());
        ASSERT_LE(last_it->size, size);
        size -= last_it->size;
        ++last_it;
      }

      allocations.erase(it, last_it);
    });
}
