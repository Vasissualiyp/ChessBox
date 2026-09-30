// SPDX-License-Identifier: GPL-3.0-or-later
//
// SAN: the notation a player reads, built from the position and the legal moves.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "io/fen.hpp"
#include "io/notation.hpp"
#include "movegen/movegen.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

CellId cell(const VariantSpec& v, int file, int rank) {
  Coord c(2);
  c.c[0] = static_cast<std::int16_t>(file);
  c.c[1] = static_cast<std::int16_t>(rank);
  return v.dims.toCell(c);
}

std::string sanOf(const VariantSpec& v, Position pos, CellId from, CellId to) {
  MoveGen gen(v);
  MoveList legal(v.moveUpperBound());
  gen.generateLegal(pos, legal);
  for (const Move& m : legal) {
    if (m.from == from && m.to == to) return sanText(v, pos, m, legal);
  }
  return "?";
}

}  // namespace

TEST_CASE("san writes the moves a player reads", "[unit][io]") {
  const VariantSpec v = test::loadVariant("standard");
  const Position start = Position::startPosition(v);
  CHECK(sanOf(v, start, cell(v, 6, 0), cell(v, 5, 2)) == "Nf3");  // g1-f3
  CHECK(sanOf(v, start, cell(v, 4, 1), cell(v, 4, 3)) == "e4");   // e2-e4
  CHECK(sanOf(v, start, cell(v, 3, 1), cell(v, 3, 3)) == "d4");   // d2-d4

  // A capture: after 1.e4 d5, exd5.
  auto pos = fromFen(v, "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
  REQUIRE(pos.has_value());
  CHECK(sanOf(v, *pos, cell(v, 4, 3), cell(v, 3, 4)) == "exd5");  // e4xd5

  // Castling, both sides.
  auto cast = fromFen(v, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  REQUIRE(cast.has_value());
  CHECK(sanOf(v, *cast, cell(v, 4, 0), cell(v, 6, 0)) == "O-O");    // e1-g1
  CHECK(sanOf(v, *cast, cell(v, 4, 0), cell(v, 2, 0)) == "O-O-O");  // e1-c1

  // Disambiguation by file: two knights can reach d2.
  auto two = fromFen(v, "7k/8/8/8/8/8/8/1N3N1K w - - 0 1");
  REQUIRE(two.has_value());
  CHECK(sanOf(v, *two, cell(v, 1, 0), cell(v, 3, 1)) == "Nbd2");
  CHECK(sanOf(v, *two, cell(v, 5, 0), cell(v, 3, 1)) == "Nfd2");
}

TEST_CASE("san marks check and mate", "[unit][io]") {
  const VariantSpec v = test::loadVariant("standard");

  // A check that is not mate: Qa1-a8, with h7 free for the king.
  auto chk = fromFen(v, "7k/8/8/8/8/8/8/Q6K w - - 0 1");
  REQUIRE(chk.has_value());
  CHECK(sanOf(v, *chk, cell(v, 0, 0), cell(v, 0, 7)) == "Qa8+");

  // Scholar's mate: Qh5xf7#.
  auto mate =
      fromFen(v, "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4");
  REQUIRE(mate.has_value());
  CHECK(sanOf(v, *mate, cell(v, 7, 4), cell(v, 5, 6)) == "Qxf7#");  // h5xf7
}

TEST_CASE("san falls back to the long form where it has no meaning", "[unit][io]") {
  // A 3-D board has no files and ranks, so the tuple form is the honest answer.
  const VariantSpec cube = test::loadVariant("cube5");
  Position p = Position::startPosition(cube);
  MoveGen gen(cube);
  MoveList legal(cube.moveUpperBound());
  gen.generateLegal(p, legal);
  REQUIRE_FALSE(legal.empty());
  for (const Move& m : legal) {
    CHECK(sanText(cube, p, m, legal) == moveText(cube, m));
    break;
  }
}
