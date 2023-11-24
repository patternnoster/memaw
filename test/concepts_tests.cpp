#include <gtest/gtest.h>
#include <memory_resource>

#include "memaw/cache_resource.hpp"
#include "memaw/chain_resource.hpp"
#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"

#include "test_resource.hpp"

using namespace memaw;

TEST(ConceptsTests, std_pmr) {
  EXPECT_TRUE(resource<std::pmr::synchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::unsynchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::monotonic_buffer_resource>);
}

template <int _int, int _sub, size_t _idx = 0>
using mock_groups =
  test_resource<resource_params{ .group = std::pair{_int, _sub} }, _idx>;

template <size_t _idx>
constexpr resource_params link1_params =
  { .nothrow_alloc = true, .nothrow_dealloc = bool(_idx % 2), .group = {1, 1} };

template <size_t _idx>
constexpr resource_params link2_params =
  { .nothrow_alloc = (_idx < 10), .nothrow_dealloc = true,
    .is_sweeping = true };

template <size_t _idx>
using chain_t = chain_resource<test_resource<link1_params<_idx>, _idx>,
                               test_resource<link2_params<_idx>, _idx>,
                               test_resource<link1_params<_idx>, _idx>>;

std::integral_constant<size_t, 0> dispatch_deallocate(const chain_t<1>&,
                                                      void*, size_t, size_t);

size_t dispatch_deallocate(const chain_t<2>&, void*, size_t, size_t) noexcept;

size_t dispatch_deallocate(const chain_t<3>&, void*, size_t, size_t) noexcept;
size_t dispatch_deallocate(const chain_t<5>&, void*, size_t, size_t);

TEST(ChainResourceTests, concepts) {
  EXPECT_TRUE((resource<chain_resource<mock_groups<0, 0>, mock_groups<0, 0, 1>,
                                       mock_groups<0, 0>>>));
  EXPECT_TRUE((resource<chain_resource<mock_groups<2, 2>, mock_groups<1, 1>,
                                       mock_groups<3, 3>>>));
  EXPECT_FALSE((resource<chain_resource<mock_groups<0, 1>,
                                        mock_groups<1, 0>>>));

  EXPECT_TRUE(nothrow_resource<chain_t<0>>);
  EXPECT_TRUE(nothrow_resource<chain_t<1>>);

  EXPECT_TRUE(resource<chain_t<2>>);
  EXPECT_FALSE(nothrow_resource<chain_t<2>>);

  EXPECT_TRUE(nothrow_resource<chain_t<3>>);

  EXPECT_TRUE(resource<chain_t<5>>);
  EXPECT_FALSE(nothrow_resource<chain_t<5>>);

  EXPECT_TRUE(nothrow_resource<chain_t<11>>);

  EXPECT_TRUE(resource<chain_resource<test_resource<>>>);
  EXPECT_FALSE(nothrow_resource<chain_resource<test_resource<>>>);

  EXPECT_TRUE(sweeping_resource<chain_t<0>>);
  EXPECT_FALSE(sweeping_resource<chain_t<1>>);
  EXPECT_FALSE(sweeping_resource<chain_t<2>>);

  EXPECT_FALSE(bound_resource<chain_t<0>>);
  EXPECT_FALSE(overaligning_resource<chain_t<0>>);

  EXPECT_TRUE((interchangeable_resource_with
                 <chain_resource<mock_groups<0, 0>, mock_groups<0, 1>,
                                 mock_groups<0, 2>>, mock_groups<0, 3>>));

  EXPECT_TRUE((substitutable_resource_for<chain_t<0>, chain_t<1>>));
  EXPECT_FALSE((substitutable_resource_for<chain_t<1>, chain_t<0>>));

  EXPECT_TRUE((substitutable_resource_for<chain_t<1>, mock_groups<2, 2>>));

  EXPECT_TRUE((substitutable_resource_for<mock_groups<0, 0>, chain_t<2>>));
  EXPECT_FALSE((substitutable_resource_for<chain_t<2>, mock_groups<2, 2>>));
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
