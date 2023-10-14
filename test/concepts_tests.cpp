#include <gtest/gtest.h>
#include <memory_resource>

#include "memaw/chain_resource.hpp"
#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"
#include "memaw/os_resource.hpp"
#include "memaw/pages_resource.hpp"

#include "test_resource.hpp"

using namespace memaw;

TEST(ConceptsTests, std_pmr) {
  EXPECT_TRUE(resource<std::pmr::synchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::unsynchronized_pool_resource>);
  EXPECT_TRUE(resource<std::pmr::monotonic_buffer_resource>);
}

template <class T>
class PagesResourcesTests: public ::testing::Test {};

using PagesResources = ::testing::Types<os_resource,
                                        regular_pages_resource,
                                        big_pages_resource,
                                        fixed_pages_resource<2_MiB>>;
TYPED_TEST_SUITE(PagesResourcesTests, PagesResources);

TYPED_TEST(PagesResourcesTests, concepts) {
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
  }(PagesResources{});
}

template <int _int, int _sub, size_t _idx = 0>
using mock_groups =
  test_resource<resource_params{ .group = std::pair{_int, _sub} }, _idx>;

template <size_t _idx>
constexpr resource_params link1_params =
  { .nothrow_alloc = true, .nothrow_dealloc = bool(_idx % 2), .group = {1, 1} };

template <size_t _idx>
constexpr resource_params link2_params =
  { .nothrow_alloc = (_idx < 10), .nothrow_dealloc = true,
    .is_sweeping = true };

template <size_t _idx>
using chain_t = chain_resource<test_resource<link1_params<_idx>, _idx>,
                               test_resource<link2_params<_idx>, _idx>,
                               test_resource<link1_params<_idx>, _idx>>;

std::integral_constant<size_t, 0> dispatch_deallocate(const chain_t<1>&,
                                                      void*, size_t, size_t);

size_t dispatch_deallocate(const chain_t<2>&, void*, size_t, size_t) noexcept;

size_t dispatch_deallocate(const chain_t<3>&, void*, size_t, size_t) noexcept;
size_t dispatch_deallocate(const chain_t<5>&, void*, size_t, size_t);

TEST(ChainResourceTests, concepts) {
  EXPECT_TRUE((resource<chain_resource<mock_groups<0, 0>, mock_groups<0, 0, 1>,
                                       mock_groups<0, 0>>>));
  EXPECT_TRUE((resource<chain_resource<mock_groups<2, 2>, mock_groups<1, 1>,
                                       mock_groups<3, 3>>>));
  EXPECT_FALSE((resource<chain_resource<mock_groups<0, 1>,
                                        mock_groups<1, 0>>>));

  EXPECT_TRUE(nothrow_resource<chain_t<0>>);
  EXPECT_TRUE(nothrow_resource<chain_t<1>>);

  EXPECT_TRUE(resource<chain_t<2>>);
  EXPECT_FALSE(nothrow_resource<chain_t<2>>);

  EXPECT_TRUE(nothrow_resource<chain_t<3>>);

  EXPECT_TRUE(resource<chain_t<5>>);
  EXPECT_FALSE(nothrow_resource<chain_t<5>>);

  EXPECT_TRUE(nothrow_resource<chain_t<11>>);

  EXPECT_TRUE(resource<chain_resource<test_resource<>>>);
  EXPECT_FALSE(nothrow_resource<chain_resource<test_resource<>>>);

  EXPECT_TRUE(sweeping_resource<chain_t<0>>);
  EXPECT_FALSE(sweeping_resource<chain_t<1>>);
  EXPECT_FALSE(sweeping_resource<chain_t<2>>);

  EXPECT_FALSE(bound_resource<chain_t<0>>);
  EXPECT_FALSE(overaligning_resource<chain_t<0>>);

  EXPECT_TRUE((interchangeable_resource_with
                 <chain_resource<mock_groups<0, 0>, mock_groups<0, 1>,
                                 mock_groups<0, 2>>, mock_groups<0, 3>>));

  EXPECT_TRUE((substitutable_resource_for<chain_t<0>, chain_t<1>>));
  EXPECT_FALSE((substitutable_resource_for<chain_t<1>, chain_t<0>>));

  EXPECT_TRUE((substitutable_resource_for<chain_t<1>, mock_groups<2, 2>>));

  EXPECT_TRUE((substitutable_resource_for<mock_groups<0, 0>, chain_t<2>>));
  EXPECT_FALSE((substitutable_resource_for<chain_t<2>, mock_groups<2, 2>>));
}
