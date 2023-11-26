#include <gtest/gtest.h>
#include <memory_resource>

#include "memaw/concepts.hpp"

#include "test_resource.hpp"

using namespace memaw;

TEST(ConceptsTests, std_pmr) {
  EXPECT_TRUE(resource<std::pmr::synchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::unsynchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::monotonic_buffer_resource>);
}
