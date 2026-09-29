// SPDX-License-Identifier: GPL-3.0-or-later
//
// Some axes are sampled more finely than a piece moves along them.
//
// The turn axis is the case that forced this: every half-move appends a board, so the
// boards along it alternate colour, and one *unit of movement* through time is two
// boards, not one. Without that, a knight travelling one turn back lands on a board
// where it is the opponent to move - a board it is not allowed to be on at all.
#include <catch2/catch_test_macros.hpp>

#include "movegen/movegen.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

int axisOf(const VariantSpec& v, std::string_view name) {
  return v.dims.axisIndex(name);
}

}  // namespace

TEST_CASE("an axis's pitch defaults to one and is data", "[unit][variant]") {
  const VariantSpec plain = test::loadVariant("standard");
  for (std::uint8_t a = 0; a < plain.dims.dims(); ++a) {
    INFO(plain.dims.name(a));
    CHECK(plain.dims.pitch(a) == 1);
  }

  const VariantSpec five = test::loadVariant("5d");
  const int turn = axisOf(five, "turn");
  const int line = axisOf(five, "line");
  REQUIRE(turn >= 0);
  REQUIRE(line >= 0);
  // Two boards per turn - one for each player - so a turn of travel is two boards.
  CHECK(five.dims.pitch(static_cast<std::size_t>(turn)) == 2);
  // A timeline hop keeps the turn, and therefore keeps whose move it is.
  CHECK(five.dims.pitch(static_cast<std::size_t>(line)) == 1);
}

TEST_CASE("no direction moves an odd number of boards through time",
          "[unit][variant]") {
  const VariantSpec v = test::loadVariant("5d");
  const auto turn = static_cast<std::size_t>(axisOf(v, "turn"));
  REQUIRE_FALSE(v.dirTable.empty());
  for (const Direction& d : v.dirTable) {
    INFO(d.toString());
    // The whole point: a piece may travel any number of *turns*, and every one of them
    // is an even number of boards.
    CHECK(d.v[turn] % 2 == 0);
  }
}

TEST_CASE("a knight travelling back in time lands on a board it may move on",
          "[unit][variant]") {
  // The reported case. The board at turn 5 is one White has just moved on, so Black is
  // to move. A knight going one turn back must arrive at turn 3 - the previous board
  // where it was Black to move - and never at turn 4, which is White's.
  const VariantSpec v = test::loadVariant("5d");
  const auto file = static_cast<std::size_t>(axisOf(v, "file"));
  const auto rank = static_cast<std::size_t>(axisOf(v, "rank"));
  const auto turn = static_cast<std::size_t>(axisOf(v, "turn"));
  const auto line = static_cast<std::size_t>(axisOf(v, "line"));

  Coord at(v.dims.dims());
  at.c[file] = 1;  // b
  at.c[rank] = 7;  // 8
  at.c[turn] = 5;
  at.c[line] = 0;

  Position p(v);
  p.clear();
  p.place(v.dims.toCell(at), v.findPiece("knight"), Color::Black);
  p.setSideToMove(Color::Black);

  MoveGen gen(v);
  MoveList moves;
  moves.reserve(v.moveUpperBound());
  gen.generatePseudoLegal(p, moves);

  bool sawB6AtTurn3 = false;
  for (const Move& m : moves) {
    const Coord to = v.dims.toCoord(m.to);
    INFO("to " << to.toString());
    // Every destination is a board with the same side to move as the one left.
    CHECK((to.c[turn] - 5) % 2 == 0);
    if (to.c[file] == 1 && to.c[rank] == 5 && to.c[turn] == 3 && to.c[line] == 0) {
      sawB6AtTurn3 = true;
    }
  }
  // And the move the player was actually trying to make is there.
  CHECK(sawB6AtTurn3);
}
