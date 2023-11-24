#include <gtest/gtest.h>

#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"
#include "memaw/os_resource.hpp"
#include "memaw/pages_resource.hpp"

using namespace memaw;

template <class T>
class OsAndPagesResourcesTests: public ::testing::Test {};

using OsAndPagesResources = ::testing::Types<os_resource,
                                             regular_pages_resource,
                                             big_pages_resource,
                                             fixed_pages_resource<2_MiB>>;
TYPED_TEST_SUITE(OsAndPagesResourcesTests, OsAndPagesResources);

TYPED_TEST(OsAndPagesResourcesTests, concepts) {
  EXPECT_TRUE(resource<TypeParam>);
  EXPECT_TRUE(bound_resource<TypeParam>);
  EXPECT_TRUE(granular_resource<TypeParam>);
  EXPECT_TRUE(overaligning_resource<TypeParam>);
  EXPECT_TRUE(sweeping_resource<TypeParam>);
  EXPECT_TRUE(thread_safe_resource<TypeParam>);
  EXPECT_TRUE(nothrow_resource<TypeParam>);

  []<typename... Ts>(const testing::Types<Ts...>) {
    const auto test = []<typename T>() {
      EXPECT_TRUE((interchangeable_resource_with<TypeParam, T>));
      EXPECT_TRUE((substitutable_resource_for<TypeParam, T>));
    };
    (test.template operator()<Ts>(), ...);
  }(OsAndPagesResources{});
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
