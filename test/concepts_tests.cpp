#include <gtest/gtest.h>
#include <memory_resource>

#include "memaw/concepts.hpp"
#include "memaw/os_resource.hpp"

using namespace memaw;

TEST(ConceptsTests, resource) {
  EXPECT_TRUE(resource<std::pmr::synchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::unsynchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::monotonic_buffer_resource>);

  EXPECT_TRUE(resource<os_resource>);
  EXPECT_TRUE(bound_resource<os_resource>);
  EXPECT_TRUE(granular_resource<os_resource>);
  EXPECT_TRUE(overaligning_resource<os_resource>);
  EXPECT_TRUE(thread_safe_resource<os_resource>);
  EXPECT_TRUE(nothrow_resource<os_resource>);
}
