#include <bit>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <latch>
#include <memory>
#include <nupp/algorithm.hpp>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"
#include "memaw/pool_resource.hpp"
#include "memaw/resource_traits.hpp"

#include "resource_test_base.hpp"
#include "test_resource.hpp"

using namespace memaw;

using testing::_;
using testing::ElementsAre;
using testing::Return;

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

  using PoolResourceTestsBase<T>::get_rand_alignment;
  using PoolResourceTestsBase<T>::get_upstream_alloc_size;
  using PoolResourceTestsBase<T>::get_upstream_alignment;

  constexpr void distribute_size(chunk_sizes_t& sizes, size_t size) noexcept {
    for (size_t chunk_id = sizes.size(); chunk_id > 0;) {
      --chunk_id;
      const auto count = size / T::chunk_sizes[chunk_id];
      sizes[chunk_id]+= count;
      size-= count * T::chunk_sizes[chunk_id];
    }
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

  void mock_upstream_alloc(const size_t count,
                           std::vector<allocation_request> adds = {}) noexcept {
    std::deque<allocation_request> allocs
      (count, { .size = this->get_upstream_alloc_size(),
                .alignment = T::config.min_chunk_size });
    for (auto add : adds) allocs.push_back(add);

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
  using upstream_traits = resource_traits<typename TypeParam::upstream_t>;

  constexpr size_t rand_allocs = 20;

  constexpr auto& chunk_sizes = TypeParam::chunk_sizes;
  constexpr auto min_chunk_size = TypeParam::config.min_chunk_size;

  constexpr auto max_size =
    TypeParam::config.max_chunk_size * TypeParam::config.chunk_size_multiplier;
  const auto bigger_size = max_size + TypeParam::config.min_chunk_size;
  const auto extra_alloc_size = resource_test::allocation_request{
    .size = upstream_traits::ceil_allocation_size(bigger_size),
    .alignment = min_chunk_size
  };

  const auto max_alignment = this->get_upstream_alignment();
  const auto bigger_alignment = max_alignment << 1;
  const auto extra_alloc_alignment = resource_test::allocation_request{
    .size = upstream_traits
            ::ceil_allocation_size(max_size + bigger_alignment - max_alignment),
    .alignment = min_chunk_size
  };

  this->mock_upstream_alloc(rand_allocs + 2,
                            std::vector{extra_alloc_size,
                                        extra_alloc_alignment});

  // First allocate for real and drain with the smallest ones
  for (size_t left_in_block = this->get_upstream_alloc_size();
       left_in_block >= min_chunk_size; left_in_block-= min_chunk_size)
    this->make_alloc(min_chunk_size, min_chunk_size,
                     this->get_rand_alignment());

  // Now try with nullptr returned
  EXPECT_CALL(this->mock, allocate(_, _)).Times(int(chunk_sizes.size()))
    .WillRepeatedly(Return(nullptr)).RetiresOnSaturation();

  for (const auto& size : chunk_sizes)
    EXPECT_EQ(this->test_pool->allocate(size), nullptr);

  EXPECT_EQ(this->allocations.size(), 1);

  // Now try unusual alignments that may force bigger blocks
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
  EXPECT_EQ(this->allocations.size(), rand_allocs + 1);
  EXPECT_LT(left_in_block, TypeParam::config.min_chunk_size);

  // Allow one more real allocation
  this->make_alloc(min_chunk_size, min_chunk_size);
  EXPECT_EQ(this->allocations.size(), rand_allocs + 2);
  left_in_block = this->get_upstream_alloc_size() -
    (this->get_upstream_alloc_size() % min_chunk_size) - min_chunk_size;

  // Now allocate bigger size than max
  this->make_alloc(bigger_size, min_chunk_size);
  EXPECT_EQ(this->allocations.size(), rand_allocs + 3);
  left_in_block+= extra_alloc_size.size - bigger_size
    - (extra_alloc_size.size % min_chunk_size);

  // Now max size but bigger alignment than max
  this->make_alloc(max_size, bigger_alignment, bigger_alignment);
  EXPECT_EQ(this->allocations.size(), rand_allocs + 4);
  left_in_block+= extra_alloc_alignment.size - max_size
    - (extra_alloc_alignment.size % min_chunk_size);

  // Now drain and make sure we didn't waste too much memory
  while (left_in_block >= min_chunk_size) {
    this->make_alloc(min_chunk_size, min_chunk_size);
    left_in_block-= min_chunk_size;
  }
  EXPECT_EQ(this->allocations.size(), rand_allocs + 4);

  // Finalize
  ASSERT_FALSE(this->has_intersections(this->pool_allocations));
  this->deallocate_all();
}

TYPED_TEST(PoolResourceTests, deallocation) {
  constexpr size_t rand_allocs = 50;
  constexpr size_t bigger_than_max_allocs = 10;
  constexpr size_t deallocs = 1000;

  constexpr auto& chunk_sizes = TypeParam::chunk_sizes;
  constexpr auto min_chunk_size = TypeParam::config.min_chunk_size;

  this->mock_upstream_alloc(rand_allocs);

  typename PoolResourceTests<TypeParam>::chunk_sizes_t sizes = {};
  size_t upstream_allocs = rand_allocs;
  size_t left_in_block = this->get_capacity(rand_allocs);

  size_t made_deallocs = 0;
  size_t made_bigger_than_max = 0;
  while (left_in_block > min_chunk_size || made_deallocs < deallocs) {
    // We may throw in some bigger-than-max allocations to make this
    // even more interesting
    if (made_bigger_than_max < bigger_than_max_allocs
        && upstream_allocs && rand() % rand_allocs == 0) {
      const auto min_alloc = chunk_sizes.back() + chunk_sizes[0];
      const auto max_alloc = this->get_upstream_alloc_size();
      size_t size = min_alloc + rand() % (max_alloc - min_alloc);
      size-= size % chunk_sizes[0];

      this->make_alloc(size, min_chunk_size, min_chunk_size);
      this->distribute_size(sizes, max_alloc - size);
      left_in_block-= size;

      --upstream_allocs;
      ++made_bigger_than_max;
      continue;
    }

    // We'll do a regular allocation
    const auto rand_id = this->get_rand_chunk_id(sizes, upstream_allocs);

    // Do we deallocate first?
    if (made_deallocs < deallocs && !this->pool_allocations.empty()
        && (!rand_id || rand() % 10 == 1)) {
      const auto dit = std::next(this->pool_allocations.begin(),
                                 rand() % this->pool_allocations.size());
      auto dit_end = std::next(dit);

      // Also throw in some sweeping deallocs if possible
      this->distribute_size(sizes, dit->size);
      size_t size = dit->size;
      while (dit_end != this->pool_allocations.end()
             && uintptr_t(dit_end->ptr) == uintptr_t(dit->ptr) + dit->size
             && (rand() % 2 == 1)) {
        this->distribute_size(sizes, dit_end->size);
        size+= dit_end->size;
        ++dit_end;
      }

      this->test_pool->deallocate(dit->ptr, size, dit->alignment);
      this->pool_allocations.erase(dit, dit_end);
      left_in_block+= size;
      ++made_deallocs;
    }

    if (rand_id) {
      // Regular allocation
      this->make_alloc(chunk_sizes[*rand_id],
                       this->get_chunk_alignment(*rand_id),
                       this->get_rand_chunk_alignment(*rand_id));
      left_in_block-= chunk_sizes[*rand_id];
    }
  }

  EXPECT_EQ(this->allocations.size(), rand_allocs);
  ASSERT_FALSE(this->has_intersections(this->pool_allocations));
  this->deallocate_all();
}

template <typename T>
class PoolResourceThreadingTests: public resource_multithreaded_test,
                                  public PoolResourceTestsBase<T> {
protected:
  struct marked_allocation {
    allocation alloc;
    uint64_t mark;

    bool operator<(const marked_allocation& rhs) const noexcept {
      return alloc.ptr < rhs.alloc.ptr
        || (alloc.ptr == rhs.alloc.ptr
            && (alloc.size < rhs.alloc.size
                || (alloc.size == rhs.alloc.size && mark < rhs.mark)));
    }
  };

  void deallocate_all(const std::set<marked_allocation>& allocs) noexcept {
    mock_deallocations(this->mock);

    std::set<allocation> smallest_allocs;
    for (const auto& alloc : allocs)
      smallest_allocs.insert(alloc.alloc);
    verify_allocations(smallest_allocs);

    this->test_pool.reset();
    EXPECT_EQ(allocations.size(), 0);
  }
};

using ThreadSafePoolResources =
  testing::Types<pool1_t<upstream1_t>, pool1_t<upstream2_t>,
                 pool1_t<upstream3_t>,
                 pool2_t<upstream1_t>, pool2_t<upstream2_t>,
                 pool2_t<upstream3_t>>;
TYPED_TEST_SUITE(PoolResourceThreadingTests, ThreadSafePoolResources);

TYPED_TEST(PoolResourceThreadingTests, randomized_multithread) {
  using allocation = PoolResourceTests<TypeParam>::allocation;
  using marked_allocation =
    PoolResourceThreadingTests<TypeParam>::marked_allocation;

  constexpr size_t threads_count = 8;
  constexpr static size_t allocs_per_thread = 1000;

  constexpr auto& chunk_sizes = TypeParam::chunk_sizes;
  constexpr static size_t min_alloc = chunk_sizes[0];
  constexpr static size_t max_alloc = chunk_sizes.back() * 2;

  const static pow2_t max_alignment =
    pow2_t{std::bit_ceil(this->get_upstream_alloc_size())};

  // We will use a mark on each chunk near the end to make sure
  // they're allocated separately
  constexpr static auto get_mark =
    [](void* const ptr, const size_t size) -> uint64_t& {
      return *reinterpret_cast<uint64_t*>(uintptr_t(ptr) + size
                                          - sizeof(uint64_t));
    };

  constexpr size_t count = threads_count * allocs_per_thread;
  this->mock_allocations
    (this->mock, count * 10, this->get_upstream_alloc_size(),
     resource_traits<typename TypeParam::upstream_t>::guaranteed_alignment());

  std::latch latch(threads_count);

  std::array<std::vector<marked_allocation>, threads_count> allocs;

  std::vector<std::thread> threads;
  threads.reserve(threads_count);

  for (size_t i = 0; i < threads_count; ++i)
    threads.emplace_back([this, &latch, &allocs](const size_t id) {
      latch.arrive_and_wait();  // All threads start at the same time
      std::vector<marked_allocation> local_allocs;

      size_t allocs_num = 0;
      while (allocs_num < allocs_per_thread || !local_allocs.empty()) {
        if (allocs_num < allocs_per_thread && (rand() % 2)) {
          // Allocate
          auto size = min_alloc + rand() % (max_alloc - min_alloc);
          size-= size % TypeParam::config.min_chunk_size;

          const auto alignment = this->get_rand_alignment(max_alignment, false);

          const auto result = this->test_pool->allocate(size, alignment);
          if (result) {
            EXPECT_EQ(result, (this->align_by(result, alignment)));
            local_allocs.emplace_back(allocation{result, size, alignment},
                                      get_mark(result, size));
            ++allocs_num;
          }
        }
        else if (!local_allocs.empty()) {
          // Deallocate
          const auto it = local_allocs.begin() + (rand() % local_allocs.size());
          ++get_mark(it->alloc.ptr, it->alloc.size);
          this->test_pool->deallocate(it->alloc.ptr, it->alloc.size,
                                      it->alloc.alignment);

          // Remember in the global, remove from local
          allocs[id].push_back(*it);
          std::swap(*it, local_allocs.back());
          local_allocs.pop_back();
        }
      }
    }, i);

  // Wait for threads to complete
  for (auto& t : threads) t.join();

  std::set<marked_allocation> allocations;
  for (const auto& vec : allocs)
    for (const auto& alloc : vec)
      allocations.insert(alloc);

  EXPECT_EQ(allocations.size(), count);
  this->deallocate_all(allocations);
}
