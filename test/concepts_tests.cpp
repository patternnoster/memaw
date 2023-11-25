#include <gtest/gtest.h>
#include <memory_resource>

#include "memaw/cache_resource.hpp"
#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"

#include "test_resource.hpp"

using namespace memaw;

TEST(ConceptsTests, std_pmr) {
  EXPECT_TRUE(resource<std::pmr::synchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::unsynchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::monotonic_buffer_resource>);
}

using upstream1_t =
  test_resource<resource_params{ .min_size = 4_KiB, .alignment = 8_KiB,
                                 .is_sweeping = true, .group = {1, 1}}>;
using upstream2_t =
  test_resource<resource_params{ .nothrow_alloc = true, .nothrow_dealloc = true,
                                 .is_sweeping = true, .is_thread_safe = true }>;

template <resource U>
using cache1_t =
  cache_resource<cache_resource_config_t<U>{ .granularity = pow2_t{1_KiB} }>;

template <resource U>
using cache2_t =
  cache_resource<cache_resource_config_t<U>{ .granularity = pow2_t{32} }>;

using res1_t = cache1_t<upstream1_t>;
using res2_t = cache1_t<upstream2_t>;
using res3_t = cache2_t<upstream1_t>;
using res4_t = cache2_t<upstream2_t>;

template <typename T>
class CacheResourceConceptsTests: public testing::Test {};
using CacheResources = testing::Types<res1_t, res2_t, res3_t, res4_t>;
TYPED_TEST_SUITE(CacheResourceConceptsTests, CacheResources);

TYPED_TEST(CacheResourceConceptsTests, concepts) {
  using upstream = TypeParam::upstream_t;

  EXPECT_TRUE(resource<TypeParam>);
  EXPECT_TRUE(nothrow_resource<TypeParam>);
  EXPECT_TRUE(bound_resource<TypeParam>);
  EXPECT_TRUE(granular_resource<TypeParam>);
  EXPECT_TRUE(sweeping_resource<TypeParam>);

  if constexpr (TypeParam::config.granularity > alignof(std::max_align_t)) {
    EXPECT_TRUE(overaligning_resource<TypeParam>);
    EXPECT_EQ(TypeParam::guaranteed_alignment(),
              TypeParam::config.granularity.value);
  }
  else {
    EXPECT_FALSE(overaligning_resource<TypeParam>);
  }

  if constexpr (std::same_as<upstream, upstream1_t>) {
    EXPECT_FALSE(thread_safe_resource<TypeParam>);
    EXPECT_FALSE((substitutable_resource_for<TypeParam, upstream2_t>));
  }
  else {
    EXPECT_TRUE(thread_safe_resource<TypeParam>);
    EXPECT_TRUE((substitutable_resource_for<TypeParam, upstream2_t>));
  }

  EXPECT_FALSE((substitutable_resource_for<TypeParam, res1_t>));
  EXPECT_FALSE((substitutable_resource_for<TypeParam, res2_t>));
  EXPECT_FALSE((substitutable_resource_for<TypeParam, res3_t>));
  EXPECT_FALSE((substitutable_resource_for<TypeParam, res4_t>));

  EXPECT_TRUE((substitutable_resource_for<TypeParam, upstream1_t>));
}
