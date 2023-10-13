#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <new>
#include <nupp/pow2_t.hpp>
#include <utility>
#include <vector>

#include "memaw/chain_resource.hpp"
#include "memaw/literals.hpp"
#include "memaw/os_resource.hpp"
#include "memaw/pages_resource.hpp"

#include "test_resource.hpp"

using namespace memaw;

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
