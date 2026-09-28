// SPDX-License-Identifier: GPL-3.0-or-later
#include "base/arena.hpp"

#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "support/alloc_tripwire.hpp"

TEST_CASE("Arena bump-allocates and respects alignment", "[unit][base]") {
  cb::Arena a(4096);
  auto* c = a.allocate<char>(1);
  auto* d = a.allocate<std::uint64_t>(3);
  REQUIRE(c != nullptr);
  REQUIRE(reinterpret_cast<std::uintptr_t>(d) % alignof(std::uint64_t) == 0);
  REQUIRE(a.used() >= sizeof(char) + 3 * sizeof(std::uint64_t));
}

TEST_CASE("Arena allocation itself does not touch the heap", "[unit][base]") {
  cb::Arena a(4096);  // the one allocation happens here, outside the scope
  cb::test::NoAllocScope guard;
  for (int i = 0; i < 100; ++i) (void)a.allocate<int>(4);
  REQUIRE(guard.clean());
}

TEST_CASE("ScratchArena rewinds on scope exit", "[unit][base]") {
  cb::Arena a(4096);
  (void)a.allocate<int>(10);
  const std::size_t base = a.used();
  {
    cb::ScratchArena s(a);
    (void)s.arena().allocate<int>(100);
    REQUIRE(a.used() > base);
  }
  REQUIRE(a.used() == base);
}

TEST_CASE("Arena tracks a high-water mark for tuning reservations", "[unit][base]") {
  cb::Arena a(4096);
  {
    cb::ScratchArena s(a);
    (void)s.arena().allocate<char>(1000);
  }
  REQUIRE(a.used() == 0);
  REQUIRE(a.highWater() >= 1000);
}
