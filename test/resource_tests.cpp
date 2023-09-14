#include <gtest/gtest.h>
#include <nupp/pow2_t.hpp>

#include "memaw/literals.hpp"
#include "memaw/os_resource.hpp"

using namespace memaw;

TEST(OsResourceTests, static_info) {
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
