#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <type_traits>
#include <utility>

#include "memaw/chain_resource.hpp"
#include "memaw/concepts.hpp"
#include "memaw/literals.hpp"

#include "test_resource.hpp"

using namespace memaw;

using testing::_;
using testing::Return;

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
using dtst_chain_t =
  chain_resource<test_resource<resource_params{ .group = {1, 3} }, _idx>,
                 test_resource<resource_params{ .group = {0, 2} }, _idx>,
                 test_resource<resource_params{ .group = {2, 2} }, _idx>>;

std::integral_constant<size_t, 2>
  dispatch_deallocate(const dtst_chain_t<1>&, void*, size_t, size_t);

size_t dispatch_deallocate(const dtst_chain_t<2>&, void*, size_t, size_t) {
  static size_t i = 0;
  return (i++) % 3;
}

TEST(ChainResourceTests, deallocation) {
  mock_resource m1, m2, m3;

  dtst_chain_t<0> chain0{test_resource(m1), test_resource(m2),
                         test_resource(m3)};
  dtst_chain_t<1> chain1{test_resource(m1), test_resource(m2),
                         test_resource(m3)};
  dtst_chain_t<2> chain2{test_resource(m1), test_resource(m2),
                         test_resource(m3)};

  EXPECT_TRUE(resource<dtst_chain_t<0>>);
  EXPECT_TRUE(resource<dtst_chain_t<1>>);
  EXPECT_TRUE(resource<dtst_chain_t<2>>);

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
