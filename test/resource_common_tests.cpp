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

  EXPECT_TRUE(traits1_t::is_sweeping);
  EXPECT_FALSE(traits2_t::is_sweeping);

  EXPECT_FALSE(traits1_t::is_overaligning);
  EXPECT_TRUE(traits2_t::is_overaligning);

  EXPECT_EQ(traits1_t::guaranteed_alignment(), alignof(std::max_align_t));
  EXPECT_EQ(traits2_t::guaranteed_alignment(), 8_KiB);

  EXPECT_TRUE(traits1_t::is_thread_safe);
  EXPECT_FALSE(traits2_t::is_thread_safe);
}
