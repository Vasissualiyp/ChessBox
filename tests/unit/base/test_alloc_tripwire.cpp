// SPDX-License-Identifier: GPL-3.0-or-later
// The tripwire's own self-test: a detector that cannot detect is worse than no
// detector, so it must be shown to fire.
#include "support/alloc_tripwire.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("allocation tripwire fires on a direct allocation", "[unit][base]") {
  // Deliberately uses ::operator new rather than a container: an unused
  // container's allocation is elidable, and an elided allocation would make this
  // self-test pass for the wrong reason.
  cb::test::NoAllocScope guard;
  void* p = ::operator new(64);
  REQUIRE(p != nullptr);
  ::operator delete(p);
  REQUIRE_FALSE(guard.clean());
  REQUIRE(guard.allocations() >= 1);
}

TEST_CASE("allocation tripwire fires on a container that escapes", "[unit][base]") {
  cb::test::NoAllocScope guard;
  std::vector<int> v;
  v.reserve(1024);
  v.push_back(7);
  // Escaping through a volatile sink stops the optimizer from removing the
  // allocation no matter what -O level the build ends up with.
  static volatile int sink = 0;
  sink = v[0];
  REQUIRE(sink == 7);
  REQUIRE_FALSE(guard.clean());
}

TEST_CASE("allocation tripwire stays quiet on stack-only work", "[unit][base]") {
  cb::test::NoAllocScope guard;
  int arr[64]{};
  for (int i = 0; i < 64; ++i) arr[i] = i * i;
  REQUIRE(arr[63] == 63 * 63);
  REQUIRE(guard.clean());
}
