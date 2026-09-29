// SPDX-License-Identifier: GPL-3.0-or-later
// FEN-N must be byte-identical to standard FEN on an ordinary 2-D board, so that
// the existing corpus of chess positions is directly usable as test data, and a
// round-trip must be exact for arbitrary variants (M1.7).
#include <catch2/catch_test_macros.hpp>

#include "io/fen.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"
#include "variant/standard.hpp"

using namespace cb;

namespace {
const char* const kCorpus[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P3/P2P1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
    "8/8/8/4k3/8/8/8/4K3 b - - 99 150",
    "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
};
}  // namespace

TEST_CASE("standard FEN round-trips byte for byte", "[golden][io]") {
  const auto v = makeStandardChess();
  for (const char* fen : kCorpus) {
    CAPTURE(fen);
    const auto p = fromFen(*v, fen);
    REQUIRE(p.has_value());
    REQUIRE(toFen(*p) == fen);
    // And the incremental hash matches a fresh computation on a parsed position.
    REQUIRE(p->hash() == p->computeHash());
    std::string why;
    REQUIRE(p->validate(&why));
  }
}

TEST_CASE("the en-passant victim is recovered from a FEN", "[golden][io]") {
  const auto v = makeStandardChess();
  const auto p =
      fromFen(*v, "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
  REQUIRE(p.has_value());
  REQUIRE(cellName(v->dims, p->epTarget()) == "f6");
  // The vulnerable pawn sits one step back along the mover's forward direction.
  REQUIRE(cellName(v->dims, p->epVictim()) == "f5");
}

TEST_CASE("a malformed FEN is rejected with a reason", "[golden][io]") {
  const auto v = makeStandardChess();
  struct Case {
    const char* fen;
    const char* fragment;
  };
  const Case cases[] = {
      {"", "at least"},
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR", "at least"},
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x - - 0 1", "side to move"},
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN w - - 0 1", "cells"},
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNRR w - - 0 1", "cells"},
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBXR w - - 0 1", "symbol"},
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w Z - 0 1", "castling"},
      {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - z9 0 1", "off the board"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.fen);
    const auto r = fromFen(*v, c.fen);
    REQUIRE_FALSE(r.has_value());
    CAPTURE(r.error().format());
    REQUIRE(r.error().message.find(c.fragment) != std::string::npos);
  }
}

TEST_CASE("cell names round-trip in both algebraic and generic form", "[golden][io]") {
  const auto v = makeStandardChess();
  for (CellId c = 0; c < v->dims.cellCount(); ++c) {
    const std::string name = cellName(v->dims, c);
    const auto back = parseCell(v->dims, name);
    REQUIRE(back.has_value());
    REQUIRE(*back == c);
  }
  REQUIRE(cellName(v->dims, 0) == "a1");
  REQUIRE(cellName(v->dims, v->dims.cellCount() - 1) == "h8");
  REQUIRE(cellName(v->dims, kInvalidCell) == "-");
}
