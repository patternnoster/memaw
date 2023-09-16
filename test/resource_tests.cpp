#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nupp/pow2_t.hpp>
#include <utility>
#include <vector>

#include "memaw/literals.hpp"
#include "memaw/os_resource.hpp"

using namespace memaw;

MATCHER_P(IsAlignedBy, log2, "") {
  return (uintptr_t(arg) & ((uintptr_t(1) << log2) - 1)) == 0;
}

class OsResourceTests: public ::testing::Test {
protected:
  template <typename T>
  void test_allocations(const T page_type) noexcept {
    constexpr size_t BaseAttempts = 10;
    for (size_t i = 0; i < BaseAttempts; ++i) {
      const auto size = get_random_size(page_type);
      void* const result =
        res.allocate(size, res.guaranteed_alignment(page_type), page_type);
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
