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

#include "test_resource.hpp"

using namespace memaw;

using testing::_;
using testing::AtMost;

using upstream1_t =
  test_resource<resource_params{ .is_sweeping = true }>;

using upstream2_t =
  test_resource<resource_params{ .min_size = 4_KiB, .alignment = 8_KiB,
                                 .is_sweeping = true}>;

using upstream3_t =
  test_resource<resource_params{ .min_size = 1000, .alignment = 32,
                                 .is_granular = true, .is_sweeping = true}>;

template <resource U, bool _thread_safe = true>
using cache1_t = cache_resource<cache_resource_config_t<U>{
  .granularity = pow2_t{1_KiB},
  .min_block_size = 1_MiB,
  .max_block_size = 4_MiB,
  .block_size_multiplier = 2.42,
  .thread_safe = _thread_safe
}>;

template <resource U, bool _thread_safe = true>
using cache2_t = cache_resource<cache_resource_config_t<U>{
  .granularity = pow2_t{32},
  .min_block_size = 1500,
  .max_block_size = 100000,
  .block_size_multiplier = 3,
  .thread_safe = _thread_safe
}>;

template <typename T>
class CacheResourceTests: public testing::Test {
protected:
  using upstream_t = T::upstream_t;

  constexpr static size_t block_alignment =
    nupp::maximum(T::config.granularity.value, upstream_t::params.alignment);

  constexpr static void* align_by(void* const ptr,
                                  const size_t alignment) noexcept {
    return reinterpret_cast<void*>((uintptr_t(ptr) + (alignment - 1))
                                   & ~(alignment - 1));
  }

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

  constexpr static std::pair<size_t, pow2_t> get_rand_alloc
    (const size_t max = T::config.min_block_size) noexcept {
    size_t size = 0;
    while (!size) {
      size = rand() % max;
      size-= size % T::config.granularity;
    }

    const auto al_shift = -2 + rand() % 5;
    const pow2_t alignment = al_shift < 0
      ? (T::config.granularity >> -al_shift)
      : (T::config.granularity << al_shift);

    return std::make_pair(size, alignment);
  }

  void mock_upstream_alloc(const size_t count,
                           const size_t additional_memory = 0) noexcept {
    // We may need to allocate real memory because it may be used in
    // the deallocator of the cache
    size_t total_block_sizes = 0;
    for (size_t i = 0; i < count; ++i)
      total_block_sizes+= get_block_size(i);

    // Now make the upstream allocation function
    const size_t req_mem = total_block_sizes + block_alignment * count
      + additional_memory;
    memory = std::make_unique_for_overwrite<std::byte[]>(req_mem);

    next_ptr = memory.get();
    EXPECT_CALL(mock, allocate(_, _)).Times(int(count))
      .WillRepeatedly([this]
                      (const size_t size, const size_t alignment) {
        EXPECT_EQ(size, get_block_size(allocations.size()));
        EXPECT_EQ(alignment, T::config.granularity);

        const auto result =
          align_by(next_ptr, nupp::maximum(alignment,
                                           upstream_t::params.alignment));
        next_ptr = (std::byte*)result + size;

        allocations.emplace(result, size, alignment);
        return reinterpret_cast<void*>(result);
      });
  }

  void deallocate_all() noexcept {
    EXPECT_CALL(mock, deallocate(_, _, _))
      .Times(AtMost(int(allocations.size())))
      .WillRepeatedly([this](void* ptr, size_t size, const size_t alignment) {
        const auto it = allocations.find({ptr, size, alignment});

        ASSERT_NE(it, allocations.end());
        ASSERT_LE(it->size, size);
        EXPECT_EQ(it->alignment, T::config.granularity);

        // Allow sweeping
        size-= it->size;
        auto last_it = std::next(it);
        while (size) {
          ASSERT_NE(last_it, allocations.end());
          ASSERT_LE(last_it->size, size);
          size-= last_it->size;
          ++last_it;
        }

        allocations.erase(it, last_it);
      });

    test_cache.reset();  // Call the destructor now
    EXPECT_EQ(allocations.size(), 0);
  }

  void SetUp() {
    test_cache = std::make_shared<T>(mock);
  }

  mock_resource mock;
  std::shared_ptr<T> test_cache;

  std::unique_ptr<std::byte[]> memory;
  std::byte* next_ptr;

  struct allocation {
    void* ptr;
    size_t size;
    size_t alignment;

    bool operator<(const allocation& rhs) const noexcept {
      return ptr < rhs.ptr;
    }
  };
  std::set<allocation> allocations;
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
  using test_t = CacheResourceTests<TypeParam>;

  constexpr size_t blocks_count = 8;

  this->mock_upstream_alloc(blocks_count);

  const auto init_ptr = this->align_by(this->memory.get(),
                                       test_t::block_alignment);
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
      next_ptr = (std::byte*)this->align_by(next_ptr + diff,
                                            test_t::block_alignment);
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

  uintptr_t end;
  while (this->allocations.size() < blocks_count) {
    const auto [size, alignment] = this->get_rand_alloc();

    const auto ptr = this->test_cache->allocate(size, alignment);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr, this->align_by(ptr, alignment));
    this->test_cache->deallocate(ptr, size, alignment);

    end = uintptr_t(ptr) + size;
  }

  // How much we can still allocate from the current block
  size_t drain_size = uintptr_t(this->next_ptr) - end;
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
    .WillRepeatedly([](const size_t, const size_t) {
      return nullptr;
    });
  EXPECT_EQ(this->test_cache->allocate(TypeParam::config.granularity.value),
            nullptr);

  this->deallocate_all();
}

TYPED_TEST(CacheResourceTests, deallocation) {
  using allocation = CacheResourceTests<TypeParam>::allocation;

  constexpr size_t blocks_count = 8;
  this->mock_upstream_alloc(blocks_count,
                            blocks_count * blocks_count  // max overalign
                            * TypeParam::config.granularity);

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
class CacheResourceThreadingTests: public CacheResourceTests<T> {};

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

  constexpr size_t count = threads_count * allocs_per_thread;

  auto blocks = std::make_unique<allocation[]>(count);
  auto block_ptrs = std::make_unique<std::unique_ptr<std::byte[]>[]>(count);
  std::atomic<size_t> last_block = 0;

  EXPECT_CALL(this->mock, allocate(_, _))
    .WillRepeatedly([this, &last_block, &blocks, &block_ptrs]
                    (const size_t size, const size_t alignment) -> void* {
      if (rand() % 3 == 0) // Make things more spicy
        return nullptr;

      const auto id = last_block.fetch_add(1, std::memory_order_relaxed);
      block_ptrs[id] = std::make_unique<std::byte[]>(size + alignment);
      blocks[id] = { this->align_by(block_ptrs[id].get(), alignment),
                     size, alignment };
      return blocks[id].ptr;
    });

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
          const auto [size, alignment] = this->get_rand_alloc(4_KiB);
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

  auto it = cache_allocs.begin();
  for (auto next_it = std::next(it); next_it != cache_allocs.end();
       ++it, ++next_it) { // No intersection
    EXPECT_GE(uintptr_t(next_it->ptr), (uintptr_t(it->ptr) + it->size));
  }

  // Copy block allocations safely now and deallocate
  this->allocations = {blocks.get(), blocks.get() + last_block.load()};

  for (const auto& alloc : cache_allocs) {
    // Out of some block
    bool found_block = false;
    for (const auto& b : this->allocations) {
      if (b.ptr <= alloc.ptr
          && ((std::byte*)b.ptr + b.size)
              >= ((std::byte*)alloc.ptr + alloc.size)) {
        found_block = true;
        break;
      }
    }
    EXPECT_TRUE(found_block);
  }

  this->deallocate_all();
}
