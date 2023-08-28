#include <bit>
#include <gtest/gtest.h>

#include "memaw/os_resource.hpp"

using namespace memaw;

TEST(OsResourceTests, static_info) {
  const auto page_size = os_resource::get_page_size();
  EXPECT_TRUE(std::has_single_bit(page_size));

  const auto granularity = os_resource::guaranteed_alignment();
  EXPECT_TRUE(std::has_single_bit(granularity));
};
