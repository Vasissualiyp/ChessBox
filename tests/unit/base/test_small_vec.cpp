// SPDX-License-Identifier: GPL-3.0-or-later
#include "base/small_vec.hpp"

#include <catch2/catch_test_macros.hpp>

#include "support/alloc_tripwire.hpp"

using cb::SmallVec;

TEST_CASE("SmallVec stores and reads back elements", "[unit][base]") {
  SmallVec<int, 4> v;
  REQUIRE(v.empty());
  v.push(7);
  v.push(9);
  REQUIRE(v.size() == 2);
  REQUIRE(v[0] == 7);
  REQUIRE(v.back() == 9);
  v.pop();
  REQUIRE(v.size() == 1);
  v.clear();
  REQUIRE(v.empty());
}

TEST_CASE("SmallVec reports overflow instead of dropping elements", "[unit][base]") {
  SmallVec<int, 2> v;
  REQUIRE(v.tryPush(1));
  REQUIRE(v.tryPush(2));
  REQUIRE_FALSE(v.tryPush(3));
  REQUIRE(v.size() == 2);  // the rejected element did not corrupt the container
}

TEST_CASE("SmallVec never allocates", "[unit][base]") {
  cb::test::NoAllocScope guard;
  SmallVec<int, 64> v;
  for (int i = 0; i < 64; ++i) v.push(i);
  int sum = 0;
  for (int x : v) sum += x;
  REQUIRE(sum == 63 * 64 / 2);
  REQUIRE(guard.clean());
}

TEST_CASE("SmallVec compares by contents", "[unit][base]") {
  SmallVec<int, 4> a{1, 2, 3};
  SmallVec<int, 4> b{1, 2, 3};
  SmallVec<int, 4> c{1, 2};
  REQUIRE(a == b);
  REQUIRE_FALSE(a == c);
}

TEST_CASE("SmallVec works at compile time", "[unit][base]") {
  constexpr auto sum = [] {
    SmallVec<int, 4> v{1, 2, 3, 4};
    int s = 0;
    for (std::size_t i = 0; i < v.size(); ++i) s += v[i];
    return s;
  }();
  static_assert(sum == 10);
  REQUIRE(sum == 10);
}
