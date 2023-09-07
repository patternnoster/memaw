#include <gtest/gtest.h>
#include <nupp/pow2_t.hpp>

#include "memaw/os_resource.hpp"

using namespace memaw;

TEST(OsResourceTests, static_info) {
  const auto page_size = os_resource::get_page_size();
  EXPECT_TRUE(nupp::is_pow2(page_size.value));

  const auto granularity = os_resource::guaranteed_alignment();
  EXPECT_TRUE(nupp::is_pow2(granularity.value));

  const auto big_page_size_opt = os_resource::get_big_page_size();
  if (big_page_size_opt) {
    EXPECT_TRUE(nupp::is_pow2(big_page_size_opt->value));
    EXPECT_LT(page_size, *big_page_size_opt);
  }

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
