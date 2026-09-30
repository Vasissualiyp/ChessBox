// SPDX-License-Identifier: GPL-3.0-or-later
// Goldens for the shipped N-dimensional variants.
//
// Unlike the standard-chess perft suite, these node counts have no external
// authority: they are this engine's own output. So they are pinned two ways that do
// have authority - the depth-1 counts were derived by hand, piece by piece, before
// being compared with the engine, and the deeper counts are checked against the
// naive oracle, which re-derives every expansion independently (ADR-0009).
//
// Anything beyond that is a regression test: the numbers' job is to notice change.
#include <catch2/catch_test_macros.hpp>

#include "io/fen.hpp"
#include "movegen/movegen.hpp"
#include "oracle/oracle.hpp"
#include "support/variants.hpp"

using namespace cb;

TEST_CASE("cube5 - a 5x5x5 board with unicorns", "[golden][dims]") {
  const VariantSpec v = test::loadVariant("cube5");
  REQUIRE(v.dims.dims() == 3);
  REQUIRE(v.dims.cellCount() == 125);
  REQUIRE(v.geom.isBox());

  // Direction counts follow from the atoms alone, and are what a reader can check:
  // rook 6, bishop 12 (planar diagonals), unicorn 8 (triagonals), knight 24. The
  // queen and king are line movers - one axis or two, never three - so 6+12 = 18,
  // and specifically *not* the 26 they would have if they also rode the unicorn's
  // triagonals. See straightLineAtoms.
  const PieceTypeId rook = v.findPiece("rook");
  const PieceTypeId bishop = v.findPiece("bishop");
  const PieceTypeId unicorn = v.findPiece("unicorn");
  const PieceTypeId knight = v.findPiece("knight");
  REQUIRE(v.pieces[rook].atoms[0].dirCount(Color::White) == 6);
  REQUIRE(v.pieces[bishop].atoms[0].dirCount(Color::White) == 12);
  REQUIRE(v.pieces[unicorn].atoms[0].dirCount(Color::White) == 8);
  REQUIRE(v.pieces[knight].atoms[0].dirCount(Color::White) == 24);
  REQUIRE(v.pieces[v.findPiece("queen")].atoms[0].dirCount(Color::White) == 6);
  REQUIRE(v.pieces[v.findPiece("queen")].atoms[1].dirCount(Color::White) == 12);
  REQUIRE(v.pieces[v.findPiece("king")].atoms[0].dirCount(Color::White) == 6);
  REQUIRE(v.pieces[v.findPiece("king")].atoms[1].dirCount(Color::White) == 12);

  // A pawn's single [1,1] capture atom yields four forward diagonals in three
  // dimensions - two inside the level and two between levels - with no extra
  // declaration anywhere. That is the generalization earning its keep.
  const PieceTypeId pawn = v.findPiece("pawn");
  REQUIRE(v.pieces[pawn].atoms[1].dirCount(Color::White) == 4);
  REQUIRE(v.pieces[pawn].atoms[1].dirCount(Color::Black) == 4);

  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  // Hand-derived: 10 pawn pushes, 12 knight, 12 bishop, 8 unicorn, 10 queen; both
  // rooks and the king are completely blocked by their own army. The queen lost the
  // four triagonal moves she used to have when she stopped being part unicorn.
  REQUIRE(gen.perft(p, 1) == 52);
  REQUIRE(gen.perft(p, 2) == 2665);
}

TEST_CASE("hyper4 - a 4x4x4x4 board", "[golden][dims]") {
  const VariantSpec v = test::loadVariant("hyper4");
  REQUIRE(v.dims.dims() == 4);
  REQUIRE(v.dims.cellCount() == 256);

  // P(4,2) * 2^2 = 48 knight directions, and 8 rook directions, from one line each.
  REQUIRE(v.pieces[v.findPiece("knight")].atoms[0].dirCount(Color::White) == 48);
  REQUIRE(v.pieces[v.findPiece("rook")].atoms[0].dirCount(Color::White) == 8);
  // Six forward capture directions for a pawn in four dimensions.
  REQUIRE(v.pieces[v.findPiece("pawn")].atoms[1].dirCount(Color::White) == 6);

  Position p = Position::startPosition(v);
  const MoveGen gen(v);
  // Hand-derived: 6 pawn captures, 12 rook, 14 knight, 7 king - the king's two
  // rank-1 steps are refused because a black pawn covers them through the level and
  // aeon axes, which is the kind of thing only a real 4-D check test finds.
  REQUIRE(gen.perft(p, 1) == 39);
  REQUIRE(gen.perft(p, 2) == 1380);
}

TEST_CASE("the N-dimensional variants agree with the naive oracle", "[golden][dims]") {
  for (const char* name : {"cube5", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const MoveGen gen(v);
    Position fast = Position::startPosition(v);
    Position slow = Position::startPosition(v);
    REQUIRE(gen.perft(fast, 2) == oracle::perft(slow, 2));
  }
}

TEST_CASE("N-dimensional positions round-trip through FEN-N", "[golden][dims]") {
  // Separators: ranks by '/', the third axis by '|', the fourth by '||'. A 2-D
  // board uses only '/', which is why standard FEN comes out byte-identical.
  for (const char* name : {"cube5", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const Position p = Position::startPosition(v);
    const std::string fen = toFen(p);
    const auto back = fromFen(v, fen);
    REQUIRE(back.has_value());
    REQUIRE(toFen(*back) == fen);
    REQUIRE(back->hash() == p.hash());
  }

  const VariantSpec cube = test::loadVariant("cube5");
  const std::string fen = toFen(Position::startPosition(cube));
  REQUIRE(fen.starts_with("5/5/5/PPPPP/RNKNR|"));
  REQUIRE(fen.find("||") == std::string::npos);  // three axes need only one '|'

  const VariantSpec hyper = test::loadVariant("hyper4");
  REQUIRE(toFen(Position::startPosition(hyper)).find("||") != std::string::npos);
}

TEST_CASE("the direction budget rejects a combinatorially explosive variant",
          "[golden][dims]") {
  // [1,2,3] at eight axes is P(8,3) * 2^3 = 2688 directions for one atom. A variant
  // that stacks enough of those must fail at load with an actionable message rather
  // than quietly allocating (M2.3).
  std::string toml = "name = \"explosive\"\n";
  for (int i = 0; i < 8; ++i) {
    toml += "[[axis]]\nname = \"a" + std::to_string(i) + "\"\nextent = 3\n";
  }
  for (int p = 0; p < 40; ++p) {
    toml += "[[piece]]\nname = \"p" + std::to_string(p) + "\"\nsymbol = \"" +
            std::string(1, static_cast<char>('A' + p % 26)) + "\"\n";
    toml += "[[piece.move]]\nvector = [1, 2, 3]\nmax = 1\nmode = \"leap\"\n";
  }
  toml += "[start]\nboard = \"";
  // 3^8 = 6561 empty cells, written as runs of 3 separated by the axis separators.
  toml += std::to_string(6561);
  toml += "\"\n";

  const auto r = loadVariantToml(toml, "<budget>");
  REQUIRE_FALSE(r.has_value());
  CAPTURE(r.error().format());
  REQUIRE(r.error().code == ErrorCode::BudgetExceeded);
  // The message has to name the piece and the atom, or an author cannot act on it.
  REQUIRE(r.error().message.find("direction table") != std::string::npos);
}
