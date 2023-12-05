#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory_resource>

#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"
#include "memaw/resource_traits.hpp"

#include "test_resource.hpp"

using namespace memaw;

using testing::_;
using testing::Return;

TEST(ConceptsTests, std_pmr) {
  EXPECT_TRUE(resource<std::pmr::synchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::unsynchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::monotonic_buffer_resource>);
}

using common_res1_t =
  test_resource<resource_params{ .nothrow_alloc = true,
                                 .nothrow_dealloc = true,
                                 .is_sweeping = true,
                                 .is_thread_safe = true }>;
using common_res2_t =
  test_resource<resource_params{ .min_size = 1024, .alignment = 8_KiB,
                                 .is_granular = true }>;

TEST(ResourceTraitsTests, concepts) {
  using traits1_t = resource_traits<common_res1_t>;
  using traits2_t = resource_traits<common_res2_t>;

  EXPECT_FALSE(traits1_t::is_bound);
  EXPECT_TRUE(traits2_t::is_bound);

  EXPECT_EQ(traits1_t::min_size(), 0);
  EXPECT_EQ(traits2_t::min_size(), 1024);

  EXPECT_FALSE(traits1_t::is_granular);
  EXPECT_TRUE(traits2_t::is_granular);

  EXPECT_EQ(traits1_t::ceil_allocation_size(42), 42);
  EXPECT_EQ(traits2_t::ceil_allocation_size(42), 1024);
  EXPECT_EQ(traits2_t::ceil_allocation_size(1025), 2048);

  EXPECT_TRUE(traits1_t::is_sweeping);
  EXPECT_FALSE(traits2_t::is_sweeping);

  EXPECT_FALSE(traits1_t::is_overaligning);
  EXPECT_TRUE(traits2_t::is_overaligning);

  EXPECT_EQ(traits1_t::guaranteed_alignment(), alignof(std::max_align_t));
  EXPECT_EQ(traits2_t::guaranteed_alignment(), 8_KiB);

  EXPECT_TRUE(traits1_t::is_thread_safe);
  EXPECT_FALSE(traits2_t::is_thread_safe);
}

TEST(ResourceTraitsTests, free_functions) {
  mock_resource non_throwing_mock, throwing_mock;
  common_res1_t res1{non_throwing_mock};
  common_res2_t res2{non_throwing_mock};
  common_res2_t res3{throwing_mock};

  EXPECT_CALL(non_throwing_mock, allocate(_, _)).Times(5)
    .WillRepeatedly(Return(nullptr));

  EXPECT_CALL(non_throwing_mock, deallocate(_, _, _)).Times(2);

  EXPECT_EQ((allocate<exceptions_policy::original>(res1, 42)), nullptr);
  EXPECT_EQ((allocate<exceptions_policy::nothrow>(res1, 42, 1024)), nullptr);

  deallocate<exceptions_policy::nothrow>(res2, nullptr, 42);
  deallocate<exceptions_policy::original>(res2, nullptr, 42, 1024);

  const auto al_res = allocate_at_least<exceptions_policy::nothrow>(res2, 42);
  EXPECT_EQ(al_res.size, 1024);

  EXPECT_THROW(((void)allocate<exceptions_policy::throw_bad_alloc>(res1, 42)),
               std::bad_alloc);
  EXPECT_THROW(((void)allocate<exceptions_policy::throw_bad_alloc>(res2, 42)),
               std::bad_alloc);

  EXPECT_CALL(throwing_mock, allocate(_, _)).Times(4)
    .WillRepeatedly([] (auto...) -> void* { throw 42; });
  EXPECT_CALL(throwing_mock, deallocate(_, _, _)).Times(2)
    .WillRepeatedly([] (auto...) { throw 42; });

  EXPECT_THROW(((void)allocate<exceptions_policy::original>(res3, 42)), int);
  EXPECT_THROW(((void)allocate<exceptions_policy::throw_bad_alloc>(res3, 42)),
               int);

  EXPECT_THROW((deallocate<exceptions_policy::original>(res3, nullptr, 42)),
               int);

  EXPECT_EQ((allocate<exceptions_policy::nothrow>(res3, 42)), nullptr);
  deallocate<exceptions_policy::nothrow>(res3, nullptr, 42);

  const auto al_res2 = allocate_at_least<exceptions_policy::nothrow>(res3, 42);
  EXPECT_EQ(al_res2.ptr, nullptr);
  EXPECT_EQ(al_res2.size, 1024);
}
