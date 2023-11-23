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
