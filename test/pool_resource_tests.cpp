#include <bit>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <nupp/algorithm.hpp>
#include <set>

#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"
#include "memaw/pool_resource.hpp"
#include "memaw/resource_traits.hpp"

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

  /**
   * @note Can return zero for no-arg (processed below)
   **/
  constexpr static size_t get_rand_alignment
    (const pow2_t max = T::config.min_chunk_size,
     const bool allow_zero = true) noexcept {
    const size_t result = rand() % (max.log2() + 1 + int(allow_zero));
    if (allow_zero && !result) return 0;
    return size_t(1) << (result - int(allow_zero));
  }

  void SetUp() {
    test_pool = std::make_shared<T>(mock);
  }

  mock_resource mock;
  std::shared_ptr<T> test_pool;
};

template <typename T>
class PoolResourceTests: public resource_test,
                         public PoolResourceTestsBase<T> {
protected:
  using upstream_t = T::upstream_t;

  using PoolResourceTestsBase<T>::get_rand_alignment;

  static size_t get_upstream_alloc_size() noexcept {
    return
      resource_traits<upstream_t>::ceil_allocation_size
        (T::config.max_chunk_size * T::config.chunk_size_multiplier);
  }

  static pow2_t get_upstream_alignment() noexcept {
    return
      nupp::maximum(T::config.min_chunk_size,
                    resource_traits<upstream_t>::guaranteed_alignment());
  }

  static pow2_t get_chunk_alignment(const size_t idx) noexcept {
    return
      nupp::minimum(pow2_t{} << std::countr_zero(T::chunk_sizes[idx]),
                    get_upstream_alignment());
  }

  static size_t get_rand_chunk_alignment(const size_t idx) noexcept {
    return get_rand_alignment(get_chunk_alignment(idx));
  }

  void mock_upstream_alloc(const size_t count) noexcept {
    std::deque<allocation_request> allocs
      (count, { .size = this->get_upstream_alloc_size(),
                .alignment = T::config.min_chunk_size });

    mock_allocations(this->mock, std::move(allocs),
                     T::upstream_t::params.alignment);
  }

  void make_alloc(const size_t size, const pow2_t expected_alignment,
                  const size_t requested_alignment = 0) {
    void* result;

    if (requested_alignment)
      result = this->test_pool->allocate(size, requested_alignment);
    else
      result = this->test_pool->allocate(size);

    EXPECT_NE(result, nullptr);
    EXPECT_EQ(result, this->align_by(result, expected_alignment));

    pool_allocations.emplace(result, size, requested_alignment);
  }

  void deallocate_all() {
    // Deallocate in random order
    while (!pool_allocations.empty()) {
      const auto it = std::next(pool_allocations.begin(),
                                rand() % pool_allocations.size());
      if (it->alignment)
        this->test_pool->deallocate(it->ptr, it->size, it->alignment);
      else
        this->test_pool->deallocate(it->ptr, it->size);

      pool_allocations.erase(it);
    }

    mock_deallocations(this->mock);
    this->test_pool.reset();  // Call the destructor now
    EXPECT_EQ(allocations.size(), 0);
  }

  std::set<allocation> pool_allocations;
};

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

TYPED_TEST(PoolResourceTests, allocation) {
  constexpr auto& chunk_sizes = TypeParam::chunk_sizes;

  this->mock_upstream_alloc(2);

  // Descending
  auto left_in_block = this->get_upstream_alloc_size();
  do {
    for (auto rit = chunk_sizes.rbegin(); rit != chunk_sizes.rend(); ++rit) {
      if (left_in_block < *rit) continue;
      const size_t idx = std::distance(rit, chunk_sizes.rend()) - 1;
      this->make_alloc(*rit, this->get_chunk_alignment(idx),
                       this->get_rand_chunk_alignment(idx));
      left_in_block-= *rit;
    }
  }
  while (left_in_block >= TypeParam::config.min_chunk_size);
  EXPECT_EQ(this->allocations.size(), 1);

  // Ascending
  left_in_block = this->get_upstream_alloc_size();
  do {
    for (auto it = chunk_sizes.begin(); it != chunk_sizes.end(); ++it) {
      if (left_in_block < *it) break;
      const size_t idx = std::distance(chunk_sizes.begin(), it);
      this->make_alloc(*it, this->get_chunk_alignment(idx),
                       this->get_rand_chunk_alignment(idx));
      left_in_block-= *it;
    }
  }
  while (left_in_block >= TypeParam::config.min_chunk_size);
  EXPECT_EQ(this->allocations.size(), 2);

  ASSERT_FALSE(this->has_intersections(this->pool_allocations));
  this->deallocate_all();
}
