// SPDX-License-Identifier: GPL-3.0-or-later
//
// M6.2: the 5d variant played through Game. A temporal game opens on one board, appends
// a board per move, and branches a timeline when a piece travels into the past.
#include <catch2/catch_test_macros.hpp>

#include "game/game.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

/// The (turn, line) board a cell belongs to, on the 5d lattice (file, rank, turn, line).
std::pair<int, int> boardOf(const VariantSpec& v, CellId c) {
  const Coord co = v.dims.toCoord(c);
  return {co.c[2], co.c[3]};
}

std::size_t visibleCells(const VariantSpec& v, const Game& g) {
  std::size_t n = 0;
  for (CellId c = 0; c < v.dims.cellCount(); ++c) {
    if (g.boardVisible(c)) ++n;
  }
  return n;
}

const Move* firstNormal(const Game& g, const VariantSpec& v) {
  for (const Move& m : g.legalMoves()) {
    if (boardOf(v, m.from) == boardOf(v, m.to)) return &m;
  }
  return nullptr;
}

}  // namespace

TEST_CASE("5d opens on one board and grows a board per move", "[unit][game]") {
  const VariantSpec v = test::loadVariant("5d");
  Game g(v);
  REQUIRE(g.isTemporal());
  REQUIRE(g.multiverse() != nullptr);
  CHECK(g.multiverse()->live().size() == 1);
  CHECK(visibleCells(v, g) == 64);  // one 8x8 board, not a grid of empty ones

  const Move* normal = firstNormal(g, v);
  REQUIRE(normal != nullptr);
  const Move chosen = *normal;
  REQUIRE(g.play(chosen).has_value());
  CHECK(g.multiverse()->live().size() == 2);
  CHECK(visibleCells(v, g) == 128);

  // The turn passed to Black, who can move on the board just created.
  CHECK(g.position().sideToMove() == Color::Black);
  bool blackCanMove = false;
  for (const Move& m : g.legalMoves()) {
    if (boardOf(v, m.from) == std::make_pair(1, 2)) blackCanMove = true;
  }
  CHECK(blackCanMove);

  // A temporal move rewrites boards wholesale, so undo restores from a snapshot.
  REQUIRE(g.undo());
  CHECK(g.multiverse()->live().size() == 1);
  CHECK(visibleCells(v, g) == 64);
}

TEST_CASE("a piece travelling into the past branches a timeline", "[unit][game]") {
  const VariantSpec v = test::loadVariant("5d");
  Game g(v);
  for (int i = 0; i < 2; ++i) {
    const Move* normal = firstNormal(g, v);
    REQUIRE(normal != nullptr);
    const Move chosen = *normal;
    REQUIRE(g.play(chosen).has_value());
  }
  const std::size_t before = g.multiverse()->live().size();

  const Move* travel = nullptr;
  for (const Move& m : g.legalMoves()) {
    if (boardOf(v, m.from) != boardOf(v, m.to)) {
      travel = &m;
      break;
    }
  }
  REQUIRE(travel != nullptr);
  const Move chosen = *travel;
  REQUIRE(g.play(chosen).has_value());
  CHECK(g.multiverse()->live().size() > before);  // a new timeline appeared
}
