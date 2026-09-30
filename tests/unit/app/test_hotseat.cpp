// SPDX-License-Identifier: GPL-3.0-or-later
//
// M7 extra: two players, one keyboard. The machine is pure - keys and a board in, a
// cell out - so it is tested with no window and no SDL.
#include <catch2/catch_test_macros.hpp>

#include "app/hotseat.hpp"
#include "app/session.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

CellId cell(const VariantSpec& v, std::initializer_list<int> coord) {
  Coord c(v.dims.dims());
  std::size_t i = 0;
  for (int x : coord) c.c[i++] = static_cast<std::int16_t>(x);
  return v.dims.toCell(c);
}

}  // namespace

TEST_CASE("a key's position is the coordinate it enters", "[unit][app]") {
  const VariantSpec v = test::loadVariant("standard");
  HotSeat hs;

  // Two keys complete a square: axes are entered in declaration order, so `qq` is
  // (file 0, rank 0).
  CHECK_FALSE(hs.feed('q', v.dims, Color::White).has_value());
  const auto square = hs.feed('q', v.dims, Color::White);
  REQUIRE(square.has_value());
  CHECK(*square == cell(v, {0, 0}));

  CHECK_FALSE(hs.feed('r', v.dims, Color::White).has_value());
  const auto far = hs.feed('r', v.dims, Color::White);
  REQUIRE(far.has_value());
  CHECK(*far == cell(v, {3, 3}));  // r is the fourth key, so index 3
}

TEST_CASE("only the player to move can enter a square", "[unit][app]") {
  const VariantSpec v = test::loadVariant("standard");
  HotSeat hs;

  // It is White's turn; Black's keys do nothing.
  CHECK_FALSE(hs.feed('y', v.dims, Color::White).has_value());
  CHECK_FALSE(hs.feed('y', v.dims, Color::White).has_value());
  CHECK(hs.partial(Color::Black).empty());

  // Black's own turn: y is the first key of Black's half.
  CHECK_FALSE(hs.feed('y', v.dims, Color::Black).has_value());
  const auto square = hs.feed('y', v.dims, Color::Black);
  REQUIRE(square.has_value());
  CHECK(*square == cell(v, {0, 0}));
}

TEST_CASE("the cancel key clears a half-typed square", "[unit][app]") {
  const VariantSpec v = test::loadVariant("standard");
  HotSeat hs;

  CHECK_FALSE(hs.feed('q', v.dims, Color::White).has_value());
  CHECK(hs.partial(Color::White).size() == 1);
  CHECK_FALSE(hs.feed(HotSeat::whiteCancel(), v.dims, Color::White).has_value());
  CHECK(hs.partial(Color::White).empty());

  // The next key starts a fresh square, so `r r` is (3,3) and not (0,3).
  CHECK_FALSE(hs.feed('r', v.dims, Color::White).has_value());
  const auto square = hs.feed('r', v.dims, Color::White);
  REQUIRE(square.has_value());
  CHECK(*square == cell(v, {3, 3}));
}

TEST_CASE("a key past the board's edge is ignored", "[unit][app]") {
  const VariantSpec cube = test::loadVariant("cube5");  // 5x5x5
  HotSeat hs;
  // 'g' is index 9, past a 5-cell axis.
  CHECK_FALSE(hs.feed('g', cube.dims, Color::White).has_value());
  CHECK(hs.partial(Color::White).empty());
}

TEST_CASE("keys are attributed to a half", "[unit][app]") {
  CHECK(HotSeat::ownerOf('q') == Color::White);
  CHECK(HotSeat::ownerOf('b') == Color::White);
  CHECK(HotSeat::ownerOf('y') == Color::Black);
  CHECK(HotSeat::ownerOf(',') == Color::Black);
  CHECK_FALSE(HotSeat::ownerOf('1').has_value());
  CHECK_FALSE(HotSeat::ownerOf(' ').has_value());
}

TEST_CASE("keys fed through the session play a move", "[unit][app]") {
  auto session = Session::create(test::loadVariant("standard"));
  REQUIRE(session.has_value());
  (*session)->setHotSeat(true);

  // e2 -> e4 on the standard board: axes are file then rank, so 't' is the e-file and
  // 'w' / 'r' are ranks 2 and 4. Each completing key is consumed.
  // Each key that belongs to player one is consumed, completing the square or not.
  CHECK((*session)->feedHotSeat('t'));
  CHECK((*session)->feedHotSeat('w'));
  CHECK((*session)->feedHotSeat('t'));
  CHECK((*session)->feedHotSeat('r'));
  CHECK((*session)->game().plyCount() == 1);

  // '1' belongs to neither half: not consumed, so the front end's shortcuts still run.
  CHECK_FALSE((*session)->feedHotSeat('1'));
}
