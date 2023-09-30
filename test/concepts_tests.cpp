#include <gtest/gtest.h>
#include <memory_resource>

#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"
#include "memaw/os_resource.hpp"
#include "memaw/pages_resource.hpp"

using namespace memaw;

TEST(ConceptsTests, std_pmr) {
  EXPECT_TRUE(resource<std::pmr::synchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::unsynchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::monotonic_buffer_resource>);
}

template <class T>
class PagesResourcesTests: public ::testing::Test {};

using PagesResources = ::testing::Types<os_resource,
                                        regular_pages_resource,
                                        big_pages_resource,
                                        fixed_pages_resource<2_MiB>>;
TYPED_TEST_SUITE(PagesResourcesTests, PagesResources);

TYPED_TEST(PagesResourcesTests, concepts) {
  EXPECT_TRUE(resource<TypeParam>);
  EXPECT_TRUE(bound_resource<TypeParam>);
  EXPECT_TRUE(granular_resource<TypeParam>);
  EXPECT_TRUE(overaligning_resource<TypeParam>);
  EXPECT_TRUE(sweeping_resource<TypeParam>);
  EXPECT_TRUE(thread_safe_resource<TypeParam>);
  EXPECT_TRUE(nothrow_resource<TypeParam>);
}
