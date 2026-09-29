// SPDX-License-Identifier: GPL-3.0-or-later
// Golden tests for the N-dimensional projection, in text.
//
// This is the same layout logic the Vulkan renderer will consume - two screen axes
// plus a laid-out grid over the remaining ones (ARCH section 10). Pinning it here
// means the renderer inherits behaviour that is already tested, and that a 4-D board
// can be read and played by hand long before any pixels exist (M2.5).
#include <catch2/catch_test_macros.hpp>

#include "io/ascii_board.hpp"
#include "support/variants.hpp"

using namespace cb;

TEST_CASE("a 2-D board renders like a chessboard", "[golden][view]") {
  const VariantSpec v = test::loadVariant("standard");
  const Position p = Position::startPosition(v);
  const std::string expected =
      "8 r n b q k b n r\n"
      "7 p p p p p p p p\n"
      "6 . . . . . . . .\n"
      "5 . . . . . . . .\n"
      "4 . . . . . . . .\n"
      "3 . . . . . . . .\n"
      "2 P P P P P P P P\n"
      "1 R N B Q K B N R\n"
      "  a b c d e f g h\n";
  REQUIRE(renderBoard(p) == expected);
}

TEST_CASE("a 3-D board renders as labelled slices", "[golden][view]") {
  const VariantSpec v = test::loadVariant("cube5");
  const Position p = Position::startPosition(v);
  const std::string out = renderBoard(p);
  // One slice per level, each labelled with the axis it is a slice of.
  REQUIRE(out.find("slice level=0\n") != std::string::npos);
  REQUIRE(out.find("slice level=4\n") != std::string::npos);
  REQUIRE(out.find("slice level=5\n") == std::string::npos);
  // White's two occupied levels and Black's, in the right places.
  REQUIRE(out.find("slice level=0\n"
                   "5 . . . . .\n"
                   "4 . . . . .\n"
                   "3 . . . . .\n"
                   "2 P P P P P\n"
                   "1 R N K N R\n") != std::string::npos);
  REQUIRE(out.find("slice level=1\n"
                   "5 . . . . .\n"
                   "4 . . . . .\n"
                   "3 . . . . .\n"
                   "2 P P P P P\n"
                   "1 B U Q U B\n") != std::string::npos);
  REQUIRE(out.find("slice level=4\n"
                   "5 r n k n r\n"
                   "4 p p p p p\n") != std::string::npos);
}

TEST_CASE("a 4-D board renders as a slice per axis combination", "[golden][view]") {
  const VariantSpec v = test::loadVariant("hyper4");
  const Position p = Position::startPosition(v);
  const std::string out = renderBoard(p);
  // 4 levels x 4 aeons = 16 slices, each naming both outer axes.
  std::size_t slices = 0;
  for (std::size_t i = out.find("slice "); i != std::string::npos;
       i = out.find("slice ", i + 1)) {
    ++slices;
  }
  REQUIRE(slices == 16);
  REQUIRE(out.find("slice level=0 aeon=0\n") != std::string::npos);
  REQUIRE(out.find("slice level=3 aeon=3\n") != std::string::npos);
  // Only the first slice holds pieces.
  REQUIRE(out.find("slice level=0 aeon=0\n"
                   "4 r n k r\n"
                   "3 p p p p\n"
                   "2 P P P P\n"
                   "1 R N K R\n") != std::string::npos);
}

TEST_CASE("the screen axes can be remapped", "[golden][view]") {
  // Choosing different screen axes is the text equivalent of turning the board in
  // the renderer, and it is what makes a 3-D position readable from another angle.
  const VariantSpec v = test::loadVariant("cube5");
  const Position p = Position::startPosition(v);
  AsciiView side;
  side.axisX = 0;  // file
  side.axisY = 2;  // level, drawn vertically
  const std::string out = renderBoard(p, side);
  REQUIRE(out.find("slice rank=0\n") != std::string::npos);
  // Viewed file-by-level at rank 0, White's two back rows stack up the low levels and
  // the rest of the column is empty - Black's back row lives at rank 4, not rank 0.
  REQUIRE(out.find("slice rank=0\n"
                   "5 . . . . .\n"
                   "4 . . . . .\n"
                   "3 . . . . .\n"
                   "2 B U Q U B\n"
                   "1 R N K N R\n") != std::string::npos);
  // And at rank 4 the same view shows Black's.
  REQUIRE(out.find("slice rank=4\n"
                   "5 r n k n r\n"
                   "4 b u q u b\n"
                   "3 . . . . .\n"
                   "2 . . . . .\n"
                   "1 . . . . .\n") != std::string::npos);
}

TEST_CASE("labels can be turned off for machine consumption", "[golden][view]") {
  const VariantSpec v = test::loadVariant("standard");
  const Position p = Position::startPosition(v);
  AsciiView bare;
  bare.labels = false;
  bare.empty = ' ';
  const std::string out = renderBoard(p, bare);
  REQUIRE(out.starts_with("r n b q k b n r\n"));
  REQUIRE(out.find("a b c d") == std::string::npos);
}
