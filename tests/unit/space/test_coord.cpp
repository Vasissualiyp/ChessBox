// SPDX-License-Identifier: GPL-3.0-or-later
#include "space/coord.hpp"

#include <catch2/catch_test_macros.hpp>

#include "support/alloc_tripwire.hpp"

using namespace cb;

TEST_CASE("Coord is a small trivially copyable POD", "[unit][space]") {
  // Movegen copies coordinates during ray walks; if this ever grows or acquires
  // a heap member, the hot path silently regresses (ADR-0003).
  static_assert(std::is_trivially_copyable_v<Coord>);
  static_assert(sizeof(Coord) <= 20);
  cb::test::NoAllocScope guard;
  Coord p = Coord::of({1, 2, 3});
  Coord q = p;
  REQUIRE(q == p);
  REQUIRE(guard.clean());
}

TEST_CASE("Coord compares dimension count as well as values", "[unit][space]") {
  REQUIRE(Coord::of({1, 2}) == Coord::of({1, 2}));
  REQUIRE_FALSE(Coord::of({1, 2}) == Coord::of({1, 2, 0}));
  REQUIRE_FALSE(Coord::of({1, 2}) == Coord::of({2, 1}));
}

TEST_CASE("Coord formats generically", "[unit][space]") {
  REQUIRE(Coord::of({3, 5, 2}).toString() == "(3,5,2)");
}

TEST_CASE("Direction precomputes its support", "[unit][space]") {
  const Direction d = Direction::make({0, 2, 0, -1, 0, 0, 0, 0}, 4);
  REQUIRE(d.nsup == 2);
  REQUIRE(d.sup[0] == 1);
  REQUIRE(d.sup[1] == 3);
  REQUIRE(d.toString() == "[0,2,0,-1]");
}

TEST_CASE("Direction negation is an involution", "[unit][space]") {
  const Direction d = Direction::make({1, -2, 3, 0, 0, 0, 0, 0}, 3);
  REQUIRE(d.negated().negated() == d);
  REQUIRE(d.negated().v[1] == 2);
  REQUIRE(d.negated().nsup == d.nsup);
}

TEST_CASE("Direction ignores components past the declared dimension count",
          "[unit][space]") {
  const Direction d = Direction::make({1, 1, 5, 0, 0, 0, 0, 0}, 2);
  REQUIRE(d.nsup == 2);
  REQUIRE(d.v[2] == 0);
}
