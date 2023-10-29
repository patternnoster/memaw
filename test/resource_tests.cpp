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
#include "memaw/chain_resource.hpp"
#include "memaw/literals.hpp"
#include "memaw/os_resource.hpp"
#include "memaw/pages_resource.hpp"

#include "test_resource.hpp"

using namespace memaw;
using std::byte;

MATCHER_P(IsAlignedBy, log2, "") {
  return (uintptr_t(arg) & ((uintptr_t(1) << log2) - 1)) == 0;
}

class OsResourceTests: public ::testing::Test {
protected:
  template <typename T>
  void test_allocs(const T page_type) noexcept {
    constexpr size_t BaseAttempts = 10;
    for (size_t i = 0; i < BaseAttempts; ++i) {
      const auto size = get_random_size(page_type);
      void* const result =
        res.allocate(size, res.guaranteed_alignment(page_type), page_type);
      alloc_test(page_type, result, size);
    }

    // Alternative interfaces
    {
      const auto size = get_random_size(page_type);
      if constexpr (std::same_as<T, page_types::regular_t>) {
        const auto result = res.allocate(size);
        alloc_test(page_type, result, size);
      }

      if constexpr (!std::same_as<T, pow2_t>) {
        const auto result = res.allocate(size, page_type);
        alloc_test(page_type, result, size);
      }
    }
  }

  template <typename T>
  void test_aligned_allocs(const T page_type) noexcept {
    for (size_t a = 1; a <= res.guaranteed_alignment(page_type); a<<= 1) {
      const auto size = get_random_size(page_type);
      auto result = res.allocate(size, a, page_type);
      alloc_test(page_type, result, size);

      // Test alternative interface too
      if constexpr (std::same_as<T, page_types::regular_t>) {
        result = res.allocate(size, a);
        alloc_test(page_type, result, size);
      }
    }
  }

  template <typename T>
  void test_overaligned_allocs(const T page_type, const int limit) noexcept {
    const auto base_align = res.guaranteed_alignment(page_type);
    for (int i = 1; i <= limit; ++i) {
      const auto size = get_random_size(page_type);
      const auto result = res.allocate(size, base_align << i, page_type);

      if (result) {
        EXPECT_THAT(result, IsAlignedBy(base_align.log2() + i));
      }

      alloc_test(page_type, result, size);
    }
  }

  void deallocate_all() noexcept {
    for (auto p : allocs) res.deallocate(p.first, p.second);
  }

  os_resource res;
  std::vector<std::pair<void*, size_t>> allocs;

private:
  template <typename T>
  size_t get_random_size(const T page_type) const noexcept {
    return res.min_size(page_type).value * (1 + (rand() % 10));
  }

  void memory_test(void* const ptr, size_t size) const noexcept {
    auto c_ptr = static_cast<char*>(ptr);
    while (size-- > 0) *c_ptr++ = 'x';
  }

  template <typename T>
  void alloc_test(const T page_type,
                  void* const ptr, const size_t size) noexcept {
    ASSERT_NE(size, 0);  // Just in case...

    allocs.emplace_back(ptr, size);
    if (!ptr) return;  // Do not test nullptr

    EXPECT_THAT(ptr, IsAlignedBy(res.guaranteed_alignment(page_type).log2()));
    memory_test(ptr, size);
  }
};

TEST_F(OsResourceTests, static_info) {
  const auto page_size = os_resource::get_page_size();
  EXPECT_TRUE(nupp::is_pow2(page_size.value));

  const auto granularity = os_resource::guaranteed_alignment();
  EXPECT_TRUE(nupp::is_pow2(granularity.value));
  EXPECT_GE(granularity, page_size);

  const auto big_page_size_opt = os_resource::get_big_page_size();
  if (big_page_size_opt) {
    EXPECT_TRUE(nupp::is_pow2(big_page_size_opt->value));
    EXPECT_LT(page_size, *big_page_size_opt);
  }

  const auto min_size_reg = os_resource::min_size();
  const auto min_size_big = os_resource::min_size(page_types::big);
  const auto min_size_exp = os_resource::min_size(pow2_t{64_MiB});

  EXPECT_EQ(min_size_reg, page_size);
  EXPECT_EQ(min_size_big, big_page_size_opt.value_or(page_size));
  EXPECT_EQ(min_size_exp, 64_MiB);

  const auto align_reg = os_resource::guaranteed_alignment(page_types::regular);
  const auto align_big = os_resource::guaranteed_alignment(page_types::big);
  const auto align_exp = os_resource::guaranteed_alignment(pow2_t{64_MiB});

  EXPECT_EQ(align_reg, granularity);
  EXPECT_GE(align_big, min_size_big);
  EXPECT_GE(align_exp, min_size_exp);

  bool has_ps = false, has_bps = false;
  for (pow2_t x : os_resource::get_available_page_sizes()) {
    EXPECT_TRUE(nupp::is_pow2(x.value));

    if (x == page_size) has_ps = true;
    else if (big_page_size_opt && x == *big_page_size_opt)
      has_bps = true;
  }

  EXPECT_TRUE(has_ps);
  EXPECT_TRUE(!big_page_size_opt || has_bps);
};

TEST_F(OsResourceTests, regular_pages) {
  constexpr auto tag = page_types::regular;

  this->test_allocs(tag);
  this->test_aligned_allocs(tag);

  for (auto p : this->allocs)
    EXPECT_NE(p.first, nullptr);  // Assume regular page allocation
                                  // succeeds...

  this->test_overaligned_allocs(tag, 10);
  this->deallocate_all();
}

TEST_F(OsResourceTests, big_pages) {
  constexpr auto tag = page_types::big;

  this->test_allocs(tag);
  this->test_aligned_allocs(tag);
  this->test_overaligned_allocs(tag, 5);
  this->deallocate_all();
}

TEST_F(OsResourceTests, explicitly_sized_pages) {
  // We will try all the pages in the list and one that isn't there
  const auto routine = [this](const pow2_t size) {
    this->test_allocs(size);
    this->test_aligned_allocs(size);

    if (size == res.get_page_size()) {
      for (auto p : this->allocs) {
        EXPECT_NE(p.first, nullptr);
      }
    }

    this->test_overaligned_allocs(size, 1);
    this->deallocate_all();
    this->allocs.clear();
  };

  uint64_t sizes_mask = 0;
  for (auto size : res.get_available_page_sizes()) {
    sizes_mask|= size;
    routine(size);
  }

  for (size_t size = res.get_page_size() << 1; size; size<<= 1) {
    if (sizes_mask & size) continue;
    routine(pow2_t{size});
    break;
  }
}

TEST(PagesResourceTests, base) {
  constexpr auto size = 2_MiB;

  os_resource os;

  regular_pages_resource reg;
  big_pages_resource big;
  fixed_pages_resource<size> fixed;

  EXPECT_EQ(reg.min_size(), os.get_page_size());
  EXPECT_EQ(big.min_size(), os.get_big_page_size().value_or(reg.min_size()));
  EXPECT_EQ(fixed.min_size(), size);

  EXPECT_GE(reg.guaranteed_alignment(), reg.min_size());
  EXPECT_GE(big.guaranteed_alignment(), big.min_size());
  EXPECT_GE(fixed.guaranteed_alignment(), fixed.min_size());

  const auto reg_ptr = reg.allocate(reg.min_size());
  EXPECT_NE(reg_ptr, nullptr);

  const auto big_ptr = big.allocate(big.min_size());
  const auto fixed_ptr = fixed.allocate(fixed.min_size());

  reg.deallocate(reg_ptr, reg.min_size());
  big.deallocate(big_ptr, big.min_size());
  fixed.deallocate(fixed_ptr, fixed.min_size());
}

template <size_t _min_size, size_t _alignment, bool _granular>
constexpr resource_params sized_resource_params =  // workaround for MSVC
  { .min_size = _min_size, .alignment = _alignment, .is_granular = _granular };

template <size_t _min_size = 0, size_t _alignment = 0, bool _granular = false>
using sized_resource =
  test_resource<sized_resource_params<_min_size, _alignment, _granular>>;

TEST(ChainResourceTests, static_info) {
  using chain1_t =
    chain_resource<sized_resource<5>, sized_resource<0>, sized_resource<7>,
                   sized_resource<0>, sized_resource<3>, sized_resource<0>>;
  using chain2_t =
    chain_resource<sized_resource<0>, sized_resource<5, 0, true>,
                   sized_resource<0>, sized_resource<7, 0, true>,
                   sized_resource<3, 0 ,true>, sized_resource<0>>;
  using chain3_t =
    chain_resource<sized_resource<8, 128>, sized_resource<5, 64, true>,
                   sized_resource<100, 128>, sized_resource<7, 256, true>>;

  EXPECT_TRUE(bound_resource<chain1_t>);
  EXPECT_TRUE(bound_resource<chain2_t>);
  EXPECT_TRUE(bound_resource<chain3_t>);

  EXPECT_EQ(chain1_t::min_size(), 7);
  EXPECT_EQ(chain2_t::min_size(), 105);
  EXPECT_EQ(chain3_t::min_size(), 700);

  EXPECT_FALSE(overaligning_resource<chain1_t>);
  EXPECT_FALSE(overaligning_resource<chain2_t>);
  EXPECT_TRUE(overaligning_resource<chain3_t>);

  EXPECT_EQ(chain3_t::guaranteed_alignment(), 64);
}

using testing::_;
using testing::Return;

TEST(ChainResourceTests, allocation) {
  mock_resource m1, m2, m3;

  constexpr resource_params nothrow_params = { .nothrow_alloc = true };
  chain_resource chain{ test_resource<nothrow_params>(m1),
                        test_resource(m2),
                        test_resource<nothrow_params, 1>(m3) };

  EXPECT_CALL(m1, allocate(_, _)).Times(3).WillRepeatedly(Return(&m1));
  EXPECT_CALL(m2, allocate(_, _)).Times(1).WillOnce(Return(&m2));
  EXPECT_CALL(m3, allocate(_, _)).Times(1).WillOnce(Return(&m3));

  EXPECT_CALL(m1, allocate(_, _)).Times(3).WillRepeatedly(Return(nullptr))
    .RetiresOnSaturation();
  EXPECT_CALL(m2, allocate(_, _)).Times(2)
    .WillOnce([](size_t, size_t) -> void* { throw std::bad_alloc{}; })
    .WillOnce(Return(nullptr)).RetiresOnSaturation();
  EXPECT_CALL(m3, allocate(_, _)).Times(1).WillOnce(Return(nullptr))
    .RetiresOnSaturation();

  using result_t = std::pair<void*, size_t>;

  EXPECT_EQ(chain.do_allocate(1), (result_t{nullptr, 2}));
  EXPECT_EQ(chain.do_allocate(1), (result_t{&m3, 2}));
  EXPECT_EQ(chain.do_allocate(1), (result_t{&m2, 1}));

  for (size_t i = 0; i < 3; ++i)
    EXPECT_EQ(chain.do_allocate(1), (result_t{&m1, 0}));
}

template <size_t _idx = 0>
using chain_t =
  chain_resource<test_resource<resource_params{ .group = {1, 3} }, _idx>,
                 test_resource<resource_params{ .group = {0, 2} }, _idx>,
                 test_resource<resource_params{ .group = {2, 2} }, _idx>>;

std::integral_constant<size_t, 2>
  dispatch_deallocate(const chain_t<1>&, void*, size_t, size_t);

size_t dispatch_deallocate(const chain_t<2>&, void*, size_t, size_t) {
  static size_t i = 0;
  return (i++) % 3;
}

TEST(ChainResourceTests, deallocation) {
  mock_resource m1, m2, m3;

  chain_t<0> chain0{test_resource(m1), test_resource(m2),
                    test_resource(m3)};
  chain_t<1> chain1{test_resource(m1), test_resource(m2),
                    test_resource(m3)};
  chain_t<2> chain2{test_resource(m1), test_resource(m2),
                    test_resource(m3)};

  EXPECT_TRUE(resource<chain_t<0>>);
  EXPECT_TRUE(resource<chain_t<1>>);
  EXPECT_TRUE(resource<chain_t<2>>);

  const auto expect_calls = [&m1, &m2, &m3]
    (const int count1, const int count2, const int count3) {
    EXPECT_CALL(m1, deallocate(_, 0, _)).Times(count1);
    EXPECT_CALL(m2, deallocate(_, 1, _)).Times(count2);
    EXPECT_CALL(m3, deallocate(_, 2, _)).Times(count3);
  };

  expect_calls(0, 1, 0);
  chain0.deallocate(nullptr, 1);

  expect_calls(0, 0, 1);
  chain1.deallocate(nullptr, 2);

  expect_calls(2, 2, 2);
  for (size_t i = 0; i < 6; ++i)
    chain2.deallocate(nullptr, i % 3);

  expect_calls(1, 0, 0);
  chain1.deallocate_with(0, nullptr, 0);
}

using testing::AtMost;

using upstream1_t =
  test_resource<resource_params{ .is_sweeping = true }>;

using upstream2_t =
  test_resource<resource_params{ .min_size = 4_KiB, .alignment = 8_KiB,
                                 .is_sweeping = true}>;

using upstream3_t =
  test_resource<resource_params{ .min_size = 1000, .alignment = 32,
                                 .is_granular = true, .is_sweeping = true}>;

template <resource U>
using cache1_t = cache_resource<cache_resource_config_t<U>{
  .granularity = pow2_t{1_KiB},
  .min_block_size = 1_MiB,
  .max_block_size = 4_MiB,
  .block_size_multiplier = 2.42
}>;

template <resource U>
using cache2_t = cache_resource<cache_resource_config_t<U>{
  .granularity = pow2_t{32},
  .min_block_size = 1500,
  .max_block_size = 100000,
  .block_size_multiplier = 3
}>;

using res1_t = cache1_t<upstream1_t>;
using res2_t = cache1_t<upstream2_t>;
using res3_t = cache1_t<upstream3_t>;
using res4_t = cache2_t<upstream1_t>;
using res5_t = cache2_t<upstream2_t>;
using res6_t = cache2_t<upstream3_t>;

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

  constexpr static size_t get_rand_alloc_size
    (const size_t max = T::config.min_block_size) noexcept {
    size_t result = 0;
    while (!result) {
      result = rand() % max;
      result-= result % T::config.granularity;
    }
    return result;
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
    memory = std::make_unique_for_overwrite<byte[]>(req_mem);

    next_ptr = memory.get();
    EXPECT_CALL(mock, allocate(_, _)).Times(int(count))
      .WillRepeatedly([this]
                      (const size_t size, const size_t alignment) {
        EXPECT_EQ(size, get_block_size(allocations.size()));
        EXPECT_TRUE(nupp::is_pow2(alignment));

        const auto result =
          align_by(next_ptr, nupp::maximum(alignment,
                                           upstream_t::params.alignment));
        next_ptr = (byte*)result + size;

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
        EXPECT_EQ(it->alignment,
                  nupp::maximum(alignment, T::config.granularity));

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

  std::unique_ptr<byte[]> memory;
  byte* next_ptr;

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
  testing::Types<res1_t, res2_t, res3_t, res4_t, res5_t, res6_t>;
TYPED_TEST_SUITE(CacheResourceTests, CacheResources);

TYPED_TEST(CacheResourceTests, allocation_base) {
  using upstream = TypeParam::upstream_t;
  using test_t = CacheResourceTests<TypeParam>;

  constexpr size_t blocks_count = 8;

  if constexpr (overaligning_resource<upstream>) {
    EXPECT_EQ(this->test_cache->guaranteed_alignment(),
              (nupp::minimum(TypeParam::config.granularity.value,
                             upstream::params.alignment)));
  }

  this->mock_upstream_alloc(blocks_count);

  const auto init_ptr = this->align_by(this->memory.get(),
                                       test_t::block_alignment);
  auto next_ptr = (byte*)init_ptr;

  size_t allocated = 0;
  size_t curr_block = 0;
  while (curr_block < (blocks_count - 1)) {
    if (rand() % 3 == 0) {
      // Throw in a bad allocation just to check
      EXPECT_EQ((this->test_cache->allocate(TypeParam::config.granularity + 1)),
                nullptr);
      continue;
    }

    const size_t to_alloc = this->get_rand_alloc_size();
    const auto alloc_result = this->test_cache->allocate(to_alloc);

    if ((allocated + to_alloc) > this->get_block_size(curr_block)) {
      const auto diff = this->get_block_size(curr_block) - allocated;
      next_ptr = (byte*)this->align_by(next_ptr + diff,
                                       test_t::block_alignment);
      ++curr_block;
      allocated = to_alloc;
    }
    else
      allocated+= to_alloc;

    EXPECT_EQ(alloc_result, next_ptr);
    next_ptr = (byte*)next_ptr + to_alloc;

    this->test_cache->deallocate(alloc_result, to_alloc);
  }

  this->deallocate_all();
}

TYPED_TEST(CacheResourceTests, allocation_corner) {
  constexpr size_t blocks_count = 4;

  // First pre-allocate some blocks
  this->mock_upstream_alloc(blocks_count);

  size_t last_alloc;
  while (this->allocations.size() < blocks_count) {
    last_alloc = this->get_rand_alloc_size();
    const auto ptr = this->test_cache->allocate(last_alloc);
    ASSERT_NE(ptr, nullptr);
    this->test_cache->deallocate(ptr, last_alloc);
  }

  // How much we can still allocate from the current block
  auto drain_size = this->get_block_size(blocks_count - 1);
  drain_size-= (drain_size % TypeParam::config.granularity) + last_alloc;

  // Now do a direct allocation
  size_t bigger_than_max = this->get_block_size(blocks_count)
    + TypeParam::config.granularity;
  bigger_than_max-= bigger_than_max % TypeParam::config.granularity;

  EXPECT_CALL(this->mock, allocate(_, _)).Times(1)
    .WillRepeatedly([bigger_than_max](const size_t size, const size_t) {
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
  auto big_alignment = TypeParam::config.granularity.value * 2;
  while (!((uintptr_t(reg_ptr) + TypeParam::config.granularity.value)
           & (big_alignment - 1)))
    big_alignment*= 2;

  const size_t reverse_calls_count = 1 +
    nupp::minimum(blocks_count,
                  size_t(std::ceil(double(this->get_block_size(blocks_count))
                                   / this->get_block_size(0)
                                   / TypeParam::config.block_size_multiplier)));

  EXPECT_CALL(this->mock, allocate(_, _)).Times(int(reverse_calls_count))
    .WillRepeatedly([big_alignment](const size_t, const size_t alignment) {
      EXPECT_EQ(alignment, big_alignment);
      return nullptr;
    });
  EXPECT_EQ((this->test_cache->allocate(TypeParam::config.granularity.value,
                                        big_alignment)), nullptr);

  // Now drain and reverse
  const auto ptr = this->test_cache->allocate(drain_size);
  EXPECT_NE(ptr, nullptr);
  this->test_cache->deallocate(ptr, drain_size);

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
    const size_t to_alloc = this->get_rand_alloc_size();
    size_t alignment = size_t(1)
      << (rand() % (TypeParam::config.granularity.log2() + 1));
    if (rand() % blocks_count == 0) // throw in overaligned
      alignment<<= rand() % blocks_count;

    const auto ptr = this->test_cache->allocate(to_alloc, alignment);
    ASSERT_NE(ptr, nullptr);
    allocs.emplace_back(ptr, to_alloc, alignment);
  }

  while (!allocs.empty()) {
    const auto it = allocs.begin() + (rand() % allocs.size());
    this->test_cache->deallocate(it->ptr, it->size, it->alignment);
    std::swap(*it, allocs.back());
    allocs.pop_back();
  }

  this->deallocate_all();
}

TYPED_TEST(CacheResourceTests, randomized_multithread) {
  using allocation = CacheResourceTests<TypeParam>::allocation;

  constexpr size_t threads_count = 8;
  constexpr static size_t allocs_per_thread = 10000;

  constexpr size_t count = threads_count * allocs_per_thread;

  auto blocks = std::make_unique<allocation[]>(count);
  auto block_ptrs = std::make_unique<std::unique_ptr<byte[]>[]>(count);
  std::atomic<size_t> last_block = 0;

  EXPECT_CALL(this->mock, allocate(_, _))
    .WillRepeatedly([this, &last_block, &blocks, &block_ptrs]
                    (const size_t size, const size_t alignment) -> void* {
      if (rand() % 3 == 0) // Make things more spicy
        return nullptr;

      const auto id = last_block.fetch_add(1, std::memory_order_relaxed);
      block_ptrs[id] = std::make_unique<byte[]>(size + alignment);
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
      bool had_overaligned = false;
      while (allocs_num < allocs_per_thread || !local_allocs.empty()) {
        if (allocs_num < allocs_per_thread && (rand() % 2)) {
          // Allocate
          const size_t to_alloc = this->get_rand_alloc_size(4_KiB);
          const size_t alignment = size_t(1)
            << (rand() % ((had_overaligned ? 1 : 2)
                          * TypeParam::config.granularity.log2()));

          if (alignment > TypeParam::config.granularity)
            had_overaligned = true;

          const auto result = this->test_cache->allocate(to_alloc, alignment);
          if (result) {
            EXPECT_EQ(result, (this->align_by(result, alignment)));
            local_allocs.emplace_back(result, to_alloc, alignment);
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
          && ((byte*)b.ptr + b.size) >= ((byte*)alloc.ptr + alloc.size)) {
        found_block = true;
        break;
      }
    }
    EXPECT_TRUE(found_block);
  }

  this->deallocate_all();
}
