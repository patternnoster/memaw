#include <array>
#include <atomic>
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <latch>
#include <memory>
#include <new>
#include <nupp/algorithm.hpp>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "memaw/cache_resource.hpp"
#include "memaw/literals.hpp"

#include "resource_test_base.hpp"
#include "test_resource.hpp"

using namespace memaw;

using testing::_;
using testing::Return;

using cct_upstream1_t =
  test_resource<resource_params{ .min_size = 4_KiB, .alignment = 8_KiB,
                                 .is_sweeping = true, .group = {1, 1}}>;
using cct_upstream2_t =
  test_resource<resource_params{ .nothrow_alloc = true, .nothrow_dealloc = true,
                                 .is_sweeping = true, .is_thread_safe = true }>;

template <resource R>
using cct_cache1_t =
  cache_resource<R, cache_resource_config{ .granularity = pow2_t{1_KiB} }>;

template <resource R>
using cct_cache2_t =
  cache_resource<R, cache_resource_config{ .granularity = pow2_t{32} }>;

using cct_res1_t = cct_cache1_t<cct_upstream1_t>;
using cct_res2_t = cct_cache1_t<cct_upstream2_t>;
using cct_res3_t = cct_cache2_t<cct_upstream1_t>;
using cct_res4_t = cct_cache2_t<cct_upstream2_t>;

TEST(CacheResourceBaseTests, construction) {
  // Construction guide & default construction
  mock_resource mock;
  cache_resource r1{cct_upstream1_t{mock}};

  cache_resource<cct_upstream1_t> r2;
  cache_resource<cct_upstream1_t,
                 cache_resource_config{ .thread_safe = false }> r3;
  EXPECT_TRUE((std::is_same_v<typename decltype(r1)::upstream_t,
                              typename decltype(r2)::upstream_t>));

  EXPECT_TRUE((std::is_same_v<typename decltype(r2)::upstream_t,
                              typename decltype(r3)::upstream_t>));

  // Move construction
  using r1_t = decltype(r1);
  constexpr static size_t second_block_size =
    r1_t::config.min_block_size * size_t(r1_t::config.block_size_multiplier);

  auto mem = std::make_unique<std::byte[]>(r1_t::config.max_block_size
                                           + second_block_size);
  EXPECT_CALL(mock, allocate(_, _)).Times(2)
    .WillOnce([&mem](const size_t size, size_t) {
      EXPECT_EQ(size, r1_t::config.min_block_size);
      return mem.get();
    })
    .WillOnce([&mem](const size_t size, size_t) {
      EXPECT_EQ(size, second_block_size);
      return mem.get() + r1_t::config.min_block_size;
    });

  const size_t size = r1_t::config.min_block_size / 4;
  for (size_t i = 0; i < 4; ++i)
    r1.deallocate(r1.allocate(size), size);

  EXPECT_CALL(mock, deallocate(_, _, _)).Times(1)
    .WillOnce([&mem](void* const ptr, const size_t size, size_t) {
      EXPECT_EQ(ptr, mem.get());
      EXPECT_EQ(size, r1_t::config.min_block_size + second_block_size);
    });

  {
    cache_resource r3 = std::move(r1);
    for (size_t i = 0; i < 2; ++i)
      r3.deallocate(r3.allocate(size), size);
  }
}

template <typename T>
class CacheResourceConceptsTests: public testing::Test {};
using CctCacheResources = testing::Types<cct_res1_t, cct_res2_t,
                                         cct_res3_t, cct_res4_t>;
TYPED_TEST_SUITE(CacheResourceConceptsTests, CctCacheResources);

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

  if constexpr (std::same_as<upstream, cct_upstream1_t>) {
    EXPECT_FALSE(thread_safe_resource<TypeParam>);
    EXPECT_FALSE((substitutable_resource_for<TypeParam, cct_upstream2_t>));
    EXPECT_FALSE((substitutable_resource_for<TypeParam, cct_res2_t>));
    EXPECT_FALSE((substitutable_resource_for<TypeParam, cct_res4_t>));
  }
  else {
    EXPECT_TRUE(thread_safe_resource<TypeParam>);
    EXPECT_TRUE((substitutable_resource_for<TypeParam, cct_upstream2_t>));
    EXPECT_TRUE((substitutable_resource_for<TypeParam, cct_res2_t>));
    EXPECT_TRUE((substitutable_resource_for<TypeParam, cct_res4_t>));
  }

  EXPECT_TRUE((substitutable_resource_for<TypeParam, cct_res1_t>));
  EXPECT_TRUE((substitutable_resource_for<TypeParam, cct_res3_t>));

  EXPECT_TRUE((substitutable_resource_for<TypeParam, cct_upstream1_t>));
}

using upstream1_t =
  test_resource<resource_params{ .is_sweeping = true }>;

using upstream2_t =
  test_resource<resource_params{ .min_size = 4_KiB, .alignment = 8_KiB,
                                 .is_sweeping = true}>;

using upstream3_t =
  test_resource<resource_params{ .min_size = 1000, .alignment = 32,
                                 .is_granular = true, .is_sweeping = true}>;

template <bool _thread_safe>
constexpr cache_resource_config cache1_config = {
  .granularity = pow2_t{1_KiB},
  .min_block_size = 1_MiB,
  .max_block_size = 4_MiB,
  .block_size_multiplier = 2.42,
  .thread_safe = _thread_safe
};

template <bool _thread_safe>
constexpr cache_resource_config cache2_config = {
  .granularity = pow2_t{32},
  .min_block_size = 1500,
  .max_block_size = 100000,
  .block_size_multiplier = 3,
  .thread_safe = _thread_safe
};

template <resource R, bool _thread_safe = true>
using cache1_t = cache_resource<R, cache1_config<_thread_safe>>;

template <resource R, bool _thread_safe = true>
using cache2_t = cache_resource<R, cache2_config<_thread_safe>>;

template <typename T>
class CacheResourceTestsBase: public testing::Test {
protected:
  constexpr static std::pair<size_t, pow2_t> get_rand_alloc
    (const size_t max_size = T::config.min_block_size,
     const size_t max_alignment = T::config.granularity << 2) noexcept {
    size_t size = T::config.granularity
      + (rand() % (max_size - T::config.granularity));
    size-= size % T::config.granularity;

    const int max_shift = (pow2_t(max_alignment)
                           / T::config.granularity).log2();
    const int shift = -max_shift + rand() % (max_shift * 2 + 1);
    const pow2_t alignment = shift < 0
      ? (T::config.granularity >> -shift)
      : (T::config.granularity << shift);

    return std::make_pair(size, alignment);
  }

  void SetUp() {
    test_cache = std::make_shared<T>(mock);
  }

  mock_resource mock;
  std::shared_ptr<T> test_cache;
};

template <typename T>
class CacheResourceTests: public resource_test,
                          public CacheResourceTestsBase<T> {
protected:
  using upstream_t = T::upstream_t;

  constexpr static size_t get_block_size(const size_t num) noexcept {
    size_t bs_lim = T::config.min_block_size;
    for (size_t i = 0; i < num; ++i)  // NB: not the same as std::pow
      bs_lim = size_t(bs_lim * T::config.block_size_multiplier);

    bs_lim = nupp::minimum(bs_lim, T::config.max_block_size);

    if constexpr (granular_resource<upstream_t>) {
      // Add some for the result to be a multiple
      const auto rem = bs_lim % upstream_t::min_size();
      if (!rem) return bs_lim;
      return bs_lim + (upstream_t::min_size() - rem);
    }
    else if constexpr (bound_resource<upstream_t>)
      return nupp::maximum(bs_lim, upstream_t::min_size());
    else
      return bs_lim;
  }

  void mock_upstream_alloc(const size_t count) noexcept {
    // We may need to allocate real memory because it may be used in
    // the deallocator of the cache
    std::deque<allocation_request> allocs;

    for (size_t i = 0; i < count; ++i)
      allocs.emplace_back(get_block_size(i), T::config.granularity);

    mock_allocations(this->mock,
                     std::move(allocs), upstream_t::params.alignment);
  }

  void deallocate_all() noexcept {
    mock_deallocations(this->mock);
    this->test_cache.reset();  // Call the destructor now
    EXPECT_EQ(allocations.size(), 0);
  }
};

using CacheResources =
  testing::Types<cache1_t<upstream1_t>, cache1_t<upstream2_t>,
                 cache1_t<upstream3_t>,
                 cache2_t<upstream1_t>, cache2_t<upstream2_t>,
                 cache2_t<upstream3_t>,
                 cache1_t<upstream1_t, false>, cache1_t<upstream2_t, false>,
                 cache1_t<upstream3_t, false>,
                 cache2_t<upstream1_t, false>, cache2_t<upstream2_t, false>,
                 cache2_t<upstream3_t, false>>;

TYPED_TEST_SUITE(CacheResourceTests, CacheResources);

TYPED_TEST(CacheResourceTests, allocation_base) {
  constexpr static size_t block_alignment =
    nupp::maximum(TypeParam::config.granularity.value,
                  TypeParam::upstream_t::params.alignment);

  constexpr size_t blocks_count = 8;

  this->mock_upstream_alloc(blocks_count);

  const auto init_ptr = this->align_by(this->get_next_ptr(), block_alignment);
  auto next_ptr = (std::byte*)init_ptr;

  size_t allocated = 0;
  size_t curr_block = 0;
  while (curr_block < (blocks_count - 1)) {
    if (rand() % 3 == 0) {
      // Throw in a bad allocation just to check
      EXPECT_EQ((this->test_cache->allocate(TypeParam::config.granularity + 1)),
                nullptr);
      continue;
    }

    const auto [to_alloc, _] = this->get_rand_alloc();
    const auto alloc_result = this->test_cache->allocate(to_alloc);

    if ((allocated + to_alloc) > this->get_block_size(curr_block)) {
      const auto diff = this->get_block_size(curr_block) - allocated;
      next_ptr = (std::byte*)this->align_by(next_ptr + diff, block_alignment);
      ++curr_block;
      allocated = to_alloc;
    }
    else
      allocated+= to_alloc;

    EXPECT_EQ(alloc_result, next_ptr);
    next_ptr = (std::byte*)next_ptr + to_alloc;

    this->test_cache->deallocate(alloc_result, to_alloc);
  }

  this->deallocate_all();
}

TYPED_TEST(CacheResourceTests, allocation_corner) {
  constexpr size_t blocks_count = 8;

  // First pre-allocate some blocks
  this->mock_upstream_alloc(blocks_count);

  uintptr_t end = 0;
  while (this->allocations.size() < blocks_count) {
    const auto [size, alignment] = this->get_rand_alloc();

    const auto ptr = this->test_cache->allocate(size, alignment);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr, this->align_by(ptr, alignment));
    this->test_cache->deallocate(ptr, size, alignment);

    end = uintptr_t(ptr) + size;
  }

  // How much we can still allocate from the current block
  size_t drain_size = uintptr_t(this->get_next_ptr()) - end;
  drain_size-= drain_size % TypeParam::config.granularity;

  // Now do a direct allocation
  size_t bigger_than_max = this->get_block_size(blocks_count)
    + TypeParam::config.granularity;
  bigger_than_max-= bigger_than_max % TypeParam::config.granularity;

  EXPECT_CALL(this->mock, allocate(_, _)).Times(1)
    .WillOnce([bigger_than_max](const size_t size, const size_t) {
      EXPECT_GE(size, bigger_than_max);
      return nullptr;
    });
  EXPECT_EQ((this->test_cache->allocate(bigger_than_max)), nullptr);

  // Make sure it didn't mess things up
  const auto reg_ptr =
    this->test_cache->allocate(TypeParam::config.granularity.value);
  EXPECT_NE(reg_ptr, nullptr);

  this->test_cache->deallocate(reg_ptr, TypeParam::config.granularity.value);
  drain_size-= TypeParam::config.granularity.value;

  // Now too big of alignment
  const auto end_ptr = uintptr_t(reg_ptr) + drain_size;
  auto big_alignment =
    pow2_t{ TypeParam::config.max_block_size, pow2_t::ceil }.value;
  while (uintptr_t(this->align_by(reg_ptr, big_alignment))
         + TypeParam::config.granularity.value < end_ptr)
    big_alignment*= 2;

  EXPECT_CALL(this->mock, allocate(_, _)).Times(1)
    .WillOnce([](const size_t, const size_t alignment) {
      EXPECT_EQ(alignment, TypeParam::config.granularity);
      return nullptr;
    });
  EXPECT_EQ((this->test_cache->allocate(TypeParam::config.granularity.value,
                                        big_alignment)), nullptr);

  // Finally drain and reverse
  const auto ptr = this->test_cache->allocate(drain_size);
  EXPECT_NE(ptr, nullptr);
  this->test_cache->deallocate(ptr, drain_size);

  size_t reverse_calls_count = 1;
  for (size_t i = 0; i < blocks_count; ++i) {
    if (this->get_block_size(i) >= TypeParam::config.max_block_size)
      break;
    ++reverse_calls_count;
  }

  EXPECT_CALL(this->mock, allocate(_, _)).Times(int(reverse_calls_count))
    .WillRepeatedly(Return(nullptr));

  EXPECT_EQ(this->test_cache->allocate(TypeParam::config.granularity.value),
            nullptr);

  this->deallocate_all();
}

TYPED_TEST(CacheResourceTests, deallocation) {
  using allocation = CacheResourceTests<TypeParam>::allocation;

  constexpr size_t blocks_count = 8;
  this->mock_upstream_alloc(blocks_count);

  // First make the allocations
  std::vector<allocation> allocs;
  while (this->allocations.size() < blocks_count) {
    const auto [size, alignment] = this->get_rand_alloc();
    const auto ptr = this->test_cache->allocate(size, alignment);
    ASSERT_NE(ptr, nullptr);
    allocs.emplace_back(ptr, size, alignment);
  }

  while (!allocs.empty()) {
    const auto it = allocs.begin() + (rand() % allocs.size());
    this->test_cache->deallocate(it->ptr, it->size, it->alignment);
    std::swap(*it, allocs.back());
    allocs.pop_back();
  }

  this->deallocate_all();
}

template <typename T>
class CacheResourceThreadingTests: public resource_multithreaded_test,
                                   public CacheResourceTestsBase<T> {
protected:
  void deallocate_all(const std::set<allocation>& allocs) noexcept {
    mock_deallocations(this->mock);
    verify_allocations(allocs);

    this->test_cache.reset();
    EXPECT_EQ(allocations.size(), 0);
  }
};

using ThreadSafeCacheResources =
  testing::Types<cache1_t<upstream1_t>, cache1_t<upstream2_t>,
                 cache1_t<upstream3_t>,
                 cache2_t<upstream1_t>, cache2_t<upstream2_t>,
                 cache2_t<upstream3_t>>;
TYPED_TEST_SUITE(CacheResourceThreadingTests, ThreadSafeCacheResources);

TYPED_TEST(CacheResourceThreadingTests, randomized_multithread) {
  using allocation = CacheResourceTests<TypeParam>::allocation;

  constexpr size_t threads_count = 8;
  constexpr static size_t allocs_per_thread = 10000;

  constexpr static size_t max_allocation = 4_KiB;
  constexpr static size_t max_alignment = TypeParam::config.granularity << 2;

  const static size_t upstream_alignment =
    resource_traits<typename TypeParam::upstream_t>::guaranteed_alignment();

  constexpr size_t count = threads_count * allocs_per_thread;
  this->mock_allocations
    (this->mock, count,
     max_allocation + nupp::maximum(max_alignment, upstream_alignment),
     upstream_alignment);

  std::latch latch(threads_count);
  std::array<std::vector<allocation>, threads_count> allocs;

  std::vector<std::thread> threads;
  threads.reserve(threads_count);

  for (size_t i = 0; i < threads_count; ++i)
    threads.emplace_back([this, &latch, &allocs](const size_t id) {
      latch.arrive_and_wait();  // All threads start at the same time
      std::vector<allocation> local_allocs;

      size_t allocs_num = 0;
      while (allocs_num < allocs_per_thread || !local_allocs.empty()) {
        if (allocs_num < allocs_per_thread && (rand() % 2)) {
          // Allocate
          const auto [size, alignment] =
            this->get_rand_alloc(max_allocation, max_alignment);
          const auto result = this->test_cache->allocate(size, alignment);
          if (result) {
            EXPECT_EQ(result, (this->align_by(result, alignment)));
            local_allocs.emplace_back(result, size, alignment);
            ++allocs_num;
          }
        }
        else {
          // Deallocate
          if (local_allocs.empty()) continue;

          const auto it = local_allocs.begin() + (rand() % local_allocs.size());
          this->test_cache->deallocate(it->ptr, it->size, it->alignment);

          // Remember in the global, remove from local
          allocs[id].push_back(*it);
          std::swap(*it, local_allocs.back());
          local_allocs.pop_back();
        }
      }
    }, i);

  // Wait for threads to complete
  for (auto& t : threads) t.join();

  // We will now sort all the allocations and make sure they don't
  // intersect and were actually allocated from the block
  std::set<allocation> cache_allocs;
  for (const auto& vec : allocs)
    for (const auto& alloc : vec)
      cache_allocs.insert(alloc);

  ASSERT_EQ(cache_allocs.size(), count);
  ASSERT_FALSE(this->has_intersections(cache_allocs));

  this->deallocate_all(cache_allocs);
}
