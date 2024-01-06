#include <bit>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <nupp/algorithm.hpp>
#include <optional>
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
  using chunk_sizes_t = std::array<size_t, T::chunk_sizes.size()>;
  using upstream_t = T::upstream_t;

  using PoolResourceTestsBase<T>::get_rand_alignment;

  constexpr void distribute_size(chunk_sizes_t& sizes, size_t size) noexcept {
    for (size_t chunk_id = sizes.size(); chunk_id > 0;) {
      --chunk_id;
      const auto count = size / T::chunk_sizes[chunk_id];
      sizes[chunk_id]+= count;
      size-= count * T::chunk_sizes[chunk_id];
    }
  }

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

  static size_t get_capacity(const size_t allocs) noexcept {
    return allocs
      * (get_upstream_alloc_size() - (get_upstream_alloc_size()
                                      % T::config.min_chunk_size));
  }

  static pow2_t get_chunk_alignment(const size_t idx) noexcept {
    return
      nupp::minimum(pow2_t{} << std::countr_zero(T::chunk_sizes[idx]),
                    get_upstream_alignment());
  }

  static size_t get_rand_chunk_alignment(const size_t idx) noexcept {
    return get_rand_alignment(get_chunk_alignment(idx));
  }

  constexpr std::optional<size_t> get_rand_chunk_id
  (chunk_sizes_t& sizes, size_t& upstream_allocs) noexcept {
    for (;;) {
      const size_t id = rand() % sizes.size();
      if (!sizes[id]) {
        for (size_t borrow_id = id + 1; borrow_id < sizes.size(); ++borrow_id) {
          if (sizes[borrow_id]) {
            --sizes[borrow_id];
            distribute_size(sizes,
                            T::chunk_sizes[borrow_id] - T::chunk_sizes[id]);
            return { id };
          }
        }

        // Couldn't borrow for this random size
        if (upstream_allocs) {
          --upstream_allocs;
          distribute_size(sizes,
                          this->get_upstream_alloc_size() - T::chunk_sizes[id]);
          return { id };
        }
        else if (id == 0) return {};
      }
      else {
        --sizes[id];
        return { id };
      }
    }
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
  constexpr size_t rand_allocs = 20;

  constexpr auto& chunk_sizes = TypeParam::chunk_sizes;

  this->mock_upstream_alloc(rand_allocs + 2);

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

  // Randomized
  typename PoolResourceTests<TypeParam>::chunk_sizes_t sizes = {};
  size_t upstream_allocs = rand_allocs;

  left_in_block = this->get_capacity(rand_allocs);
  for (;;) {
    const auto rand_id = this->get_rand_chunk_id(sizes, upstream_allocs);
    if (!rand_id) break;

    this->make_alloc(chunk_sizes[*rand_id],
                     this->get_chunk_alignment(*rand_id),
                     this->get_rand_chunk_alignment(*rand_id));
    left_in_block-= chunk_sizes[*rand_id];
  }
  EXPECT_EQ(this->allocations.size(), rand_allocs + 2);
  EXPECT_LT(left_in_block, TypeParam::config.min_chunk_size);

  ASSERT_FALSE(this->has_intersections(this->pool_allocations));
  this->deallocate_all();
}

TYPED_TEST(PoolResourceTests, allocation_corner) {
  constexpr size_t rand_allocs = 20;

  constexpr auto& chunk_sizes = TypeParam::chunk_sizes;
  constexpr auto min_chunk_size = TypeParam::config.min_chunk_size;

  this->mock_upstream_alloc(rand_allocs);

  typename PoolResourceTests<TypeParam>::chunk_sizes_t sizes = {};
  size_t upstream_allocs = rand_allocs;
  size_t left_in_block = this->get_capacity(rand_allocs);
  for (;;) {
    const auto rand_id = this->get_rand_chunk_id(sizes, upstream_allocs);
    if (!rand_id) break;  // No more memory

    const auto chunk_id = *rand_id;
    const auto chunk_alignment = this->get_chunk_alignment(chunk_id);

    if (chunk_id == 0 || (rand() % 3) == 1) {
      // We will either request this size with an affordable
      // alignment...
      const auto alignment = this->get_rand_alignment(chunk_alignment, false);

      // TODO: optionally leave some out
      const size_t extra_chunks = chunk_id == 0 ? 1
        : (chunk_sizes[chunk_id] - chunk_sizes[chunk_id - 1]) / min_chunk_size;
      const size_t to_leave = rand() % extra_chunks;
      const size_t size = chunk_sizes[chunk_id] - to_leave * min_chunk_size;

      this->make_alloc(size, pow2_t{alignment}, alignment);
      left_in_block-= size;
      sizes[0]+= to_leave;
    }
    else {
      // ... or request a smaller size with a bigger alignment
      const auto req_chunk_id = rand() % chunk_id;

      const auto min_padding =
        chunk_sizes[chunk_id - 1] - chunk_sizes[req_chunk_id] + 1;
      const auto max_padding =
        chunk_sizes[chunk_id] - chunk_sizes[req_chunk_id];

      const auto min_req_alignment =
        pow2_t{std::bit_ceil(min_padding
                             + this->get_chunk_alignment(chunk_id-1))};
      const auto max_req_alignment =
        pow2_t{std::bit_floor(max_padding
                              + this->get_chunk_alignment(chunk_id))};

      const auto alignment = min_req_alignment *
        this->get_rand_alignment(max_req_alignment / min_req_alignment, false);

      this->make_alloc(chunk_sizes[req_chunk_id], pow2_t{alignment}, alignment);

      // To be safe we'll distribute padding into the smallest chunks
      const auto real_padding =
        chunk_sizes[chunk_id] - chunk_sizes[req_chunk_id];
      sizes[0]+= real_padding / chunk_sizes[0];

      left_in_block-= chunk_sizes[req_chunk_id];
    }
  }
  EXPECT_EQ(this->allocations.size(), rand_allocs);
  EXPECT_LT(left_in_block, TypeParam::config.min_chunk_size);

  // Finalize
  ASSERT_FALSE(this->has_intersections(this->pool_allocations));
  this->deallocate_all();
}
