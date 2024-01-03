#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "memaw/literals.hpp"
#include "memaw/pool_resource.hpp"

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
