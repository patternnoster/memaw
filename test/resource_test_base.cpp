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

void resource_multithreaded_test::mock_allocations
  (mock_resource& mock, const size_t count, const size_t max_size,
   const size_t min_align) {
  const size_t total_size = count * max_size * 2;

  blocks_ = std::make_unique_for_overwrite<allocation[]>(count);
  memory_ = std::make_unique_for_overwrite<std::byte[]>(total_size);

  last_block_.store(0);
  next_ptr_.store(memory_.get());

  EXPECT_CALL(mock, allocate(_, _))
    .WillRepeatedly([this, min_align](const size_t size,
                                      const size_t alignment) -> void* {
      if (rand() % 3 == 0) // Make things more spicy
        return nullptr;

      // Make this as simple as possible
      const auto id = last_block_.fetch_add(1, std::memory_order_relaxed);

      const auto real_alignment = nupp::maximum(alignment, min_align);

      const auto next_ptr =
        next_ptr_.fetch_add(size + real_alignment, std::memory_order_relaxed);
      const auto result = align_by(next_ptr, real_alignment);

      blocks_[id] = { result, size, alignment };
      return result;
    });
}

void resource_multithreaded_test::mock_deallocations(mock_resource& mock) {
  allocations = { blocks_.get(), blocks_.get() + last_block_.load() };
  resource_test_base::mock_deallocations(mock);
}
