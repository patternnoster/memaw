#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>

#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"
#include "memaw/pool_resource.hpp"

#include "resource_test_base.hpp"
#include "test_resource.hpp"

using namespace memaw;

using testing::ElementsAre;

TEST(PoolResourceStaticTests, chunk_sizes) {
  using upstream_t = test_resource<resource_params{ .is_sweeping = true }>;

  using pr1_t =
    pool_resource<upstream_t,
                  pool_resource_config{ .min_chunk_size = pow2_t{2_KiB},
                                        .max_chunk_size = 16_KiB,
                                        .chunk_size_multiplier = 2 }>;
  EXPECT_THAT(pr1_t::chunk_sizes, ElementsAre(2_KiB, 4_KiB, 8_KiB, 16_KiB));

  using pr2_t =
    pool_resource<upstream_t,
                  pool_resource_config{ .min_chunk_size = pow2_t{8_KiB},
                                        .max_chunk_size = 216_KiB,
                                        .chunk_size_multiplier = 3 }>;
  EXPECT_THAT(pr2_t::chunk_sizes, ElementsAre(8_KiB, 24_KiB, 72_KiB, 216_KiB));

  using pr3_t =  // border case
    pool_resource<upstream_t,
                  pool_resource_config{ .min_chunk_size = pow2_t{1_KiB},
                                        .max_chunk_size = 1_KiB,
                                        .chunk_size_multiplier = 1 }>;
  EXPECT_THAT(pr3_t::chunk_sizes, ElementsAre(1_KiB));
}


using upstream1_t =  // Simple no limits
  test_resource<resource_params{ .is_sweeping = true }>;

using upstream2_t =  // Classical
  test_resource<resource_params{ .min_size = 4_KiB, .alignment = 4_KiB,
                                 .is_granular = true, .is_sweeping = true}>;

using upstream3_t =  // Non-granular
  test_resource<resource_params{ .min_size = 40000, .is_sweeping = true}>;

using upstream4_t =  // Weird limits
  test_resource<resource_params{ .min_size = 1000, .alignment = 32,
                                 .is_granular = true, .is_sweeping = true}>;

template <bool _thread_safe>
constexpr pool_resource_config pool1_config = {
  .min_chunk_size = pow2_t{1024},
  .max_chunk_size = 8192,
  .chunk_size_multiplier = 2,
  .thread_safe = _thread_safe
};

template <bool _thread_safe>
constexpr pool_resource_config pool2_config = {
  .min_chunk_size = pow2_t{128},
  .max_chunk_size = 10368,
  .chunk_size_multiplier = 3,
  .thread_safe = _thread_safe
};

template <resource R, bool _thread_safe = true>
using pool1_t = pool_resource<R, pool1_config<_thread_safe>>;

template <resource R, bool _thread_safe = true>
using pool2_t = pool_resource<R, pool2_config<_thread_safe>>;

template <typename T>
class PoolResourceTestsBase: public testing::Test {
protected:
  using upstream_t = T::upstream_t;

  void SetUp() {
    test_pool = std::make_shared<T>(mock);
  }

  mock_resource mock;
  std::shared_ptr<T> test_pool;
};

template <typename T>
class PoolResourceTests: public resource_test,
                         public PoolResourceTestsBase<T> {};

using PoolResources =
  testing::Types<pool1_t<upstream1_t>, pool1_t<upstream2_t>,
                 pool1_t<upstream3_t>, pool1_t<upstream4_t>,
                 pool2_t<upstream1_t>, pool2_t<upstream2_t>,
                 pool2_t<upstream3_t>, pool2_t<upstream4_t>,
                 pool1_t<upstream1_t, false>, pool1_t<upstream2_t, false>,
                 pool1_t<upstream3_t, false>, pool1_t<upstream4_t, false>,
                 pool2_t<upstream1_t, false>, pool2_t<upstream2_t, false>,
                 pool2_t<upstream3_t, false>, pool2_t<upstream4_t, false>>;
TYPED_TEST_SUITE(PoolResourceTests, PoolResources);

TYPED_TEST(PoolResourceTests, concepts) {
  EXPECT_TRUE(resource<TypeParam>);
  EXPECT_TRUE(nothrow_resource<TypeParam>);
  EXPECT_TRUE(bound_resource<TypeParam>);
  EXPECT_TRUE(granular_resource<TypeParam>);
  EXPECT_TRUE(sweeping_resource<TypeParam>);

  if constexpr (TypeParam::config.min_chunk_size > alignof(std::max_align_t)) {
    EXPECT_TRUE(overaligning_resource<TypeParam>);
    EXPECT_EQ(TypeParam::guaranteed_alignment(),
              TypeParam::config.min_chunk_size);
  }
  else {
    EXPECT_FALSE(overaligning_resource<TypeParam>);
  }

  EXPECT_EQ(thread_safe_resource<TypeParam>,
            (TypeParam::config.thread_safe
             && thread_safe_resource<typename TypeParam::upstream_t>));
}
