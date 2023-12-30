#include "resource_test_base.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <nupp/algorithm.hpp>

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

void resource_test::mock_allocations(mock_resource& mock,
                                     std::deque<allocation_request>&& reqs,
                                     const size_t min_align) {
  requests_ = std::move(reqs);

  size_t total_size = 0;
  for (const auto& req : requests_)
    total_size+= req.size + nupp::maximum(req.alignment, min_align);

  memory_ = std::make_unique_for_overwrite<std::byte[]>(total_size);
  next_ptr_ = memory_.get();

  EXPECT_CALL(mock, allocate(_, _)).Times(int(requests_.size()))
    .WillRepeatedly([this, min_align](const size_t size,
                                      const size_t alignment) -> void* {
      EXPECT_FALSE(requests_.empty());
      if (requests_.empty())
        return nullptr;

      EXPECT_EQ(size, requests_.front().size);
      EXPECT_EQ(alignment, requests_.front().alignment);

      const auto result =
        align_by(next_ptr_, nupp::maximum(alignment, min_align));

      next_ptr_ = (std::byte*)result + size;

      requests_.pop_front();
      allocations.emplace(result, size, alignment);

      return reinterpret_cast<void*>(result);
    });
}
