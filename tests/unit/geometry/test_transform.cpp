// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/transform.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cb;

namespace {
Coord c3(int x, int y, int z) { return Coord::of({x, y, z}); }
Direction d3(int x, int y, int z) {
  return Direction::make({static_cast<std::int16_t>(x), static_cast<std::int16_t>(y),
                          static_cast<std::int16_t>(z), 0, 0, 0, 0, 0},
                         3);
}
}  // namespace

TEST_CASE("identity transform leaves coordinates and directions alone",
          "[unit][geometry]") {
  const Transform t = Transform::identity(3);
  REQUIRE(t.isIdentity());
  REQUIRE(t.applyToCoord(c3(1, 2, 3)) == c3(1, 2, 3));
  REQUIRE(t.applyToDir(d3(1, -1, 0)) == d3(1, -1, 0));
  REQUIRE_FALSE(t.reversesOrientation());
}

TEST_CASE("a wrap transform shifts one coordinate and leaves direction intact",
          "[unit][geometry]") {
  Transform t = Transform::identity(2);
  t.off[0] = -8;  // leaving x=8 re-enters at x=0
  REQUIRE(t.applyToCoord(Coord::of({8, 3})) == Coord::of({0, 3}));
  // A pure translation must not touch the direction: a rook crossing a cylinder
  // seam keeps travelling along the same rank.
  REQUIRE(t.applyToDir(Direction::make({1, 0, 0, 0, 0, 0, 0, 0}, 2)) ==
          Direction::make({1, 0, 0, 0, 0, 0, 0, 0}, 2));
  REQUIRE_FALSE(t.reversesOrientation());
}

TEST_CASE("a flip transform reverses the direction component it flips",
          "[unit][geometry]") {
  // This is the property that makes non-orientable boards correct: the DIRECTION
  // is transported, not merely the position (ADR-0004).
  Transform t = Transform::identity(2);
  t.sign[0] = -1;
  t.off[0] = 7;  // x -> 7 - x on an extent-8 axis
  REQUIRE(t.applyToCoord(Coord::of({0, 1})) == Coord::of({7, 1}));
  REQUIRE(t.applyToCoord(Coord::of({7, 1})) == Coord::of({0, 1}));
  REQUIRE(t.applyToDir(Direction::make({1, 1, 0, 0, 0, 0, 0, 0}, 2)) ==
          Direction::make({-1, 1, 0, 0, 0, 0, 0, 0}, 2));
  REQUIRE(t.reversesOrientation());
}

TEST_CASE("an axis swap permutes coordinates and directions", "[unit][geometry]") {
  Transform t = Transform::identity(3);
  t.src[0] = 1;
  t.src[1] = 0;
  REQUIRE(t.applyToCoord(c3(1, 2, 3)) == c3(2, 1, 3));
  REQUIRE(t.applyToDir(d3(1, 2, 0)) == d3(2, 1, 0));
  REQUIRE(t.reversesOrientation());  // a single exchange is an odd permutation
}

TEST_CASE("transform composition agrees with sequential application",
          "[unit][geometry]") {
  Transform a = Transform::identity(3);
  a.sign[0] = -1;
  a.off[0] = 7;
  Transform b = Transform::identity(3);
  b.src[1] = 2;
  b.src[2] = 1;
  b.off[1] = 3;

  const Transform ab = a.compose(b);
  for (int x = 0; x < 4; ++x) {
    for (int y = 0; y < 4; ++y) {
      for (int z = 0; z < 4; ++z) {
        REQUIRE(ab.applyToCoord(c3(x, y, z)) == a.applyToCoord(b.applyToCoord(c3(x, y, z))));
        REQUIRE(ab.applyToDir(d3(x, y, z)) == a.applyToDir(b.applyToDir(d3(x, y, z))));
      }
    }
  }
}

TEST_CASE("transform inverse undoes the transform", "[unit][geometry]") {
  Transform t = Transform::identity(3);
  t.src[0] = 2;
  t.src[2] = 0;
  t.sign[1] = -1;
  t.off[1] = 5;
  t.off[2] = -3;

  const Transform inv = t.inverse();
  REQUIRE(inv.compose(t).isIdentity());
  REQUIRE(t.compose(inv).isIdentity());
  for (int x = 0; x < 4; ++x) {
    for (int y = 0; y < 4; ++y) {
      REQUIRE(inv.applyToCoord(t.applyToCoord(c3(x, y, 1))) == c3(x, y, 1));
    }
  }
}

TEST_CASE("orientation reversal counts signs and permutation parity together",
          "[unit][geometry]") {
  Transform two = Transform::identity(3);
  two.sign[0] = -1;
  two.sign[1] = -1;
  REQUIRE_FALSE(two.reversesOrientation());  // two flips compose to a rotation

  Transform mixed = Transform::identity(3);
  mixed.sign[0] = -1;
  mixed.src[1] = 2;
  mixed.src[2] = 1;
  REQUIRE_FALSE(mixed.reversesOrientation());  // odd permutation x one flip
}
