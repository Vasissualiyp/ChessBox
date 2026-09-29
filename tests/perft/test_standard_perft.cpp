// SPDX-License-Identifier: GPL-3.0-or-later
// The hard gate of M1: standard chess, defined entirely in terms of the general
// vector-move algebra, must reproduce the published perft node counts exactly.
//
// These numbers are external ground truth (the widely replicated CPW perft
// results). They are never regenerated to make a build pass - a mismatch is
// bisected with perftDivide against the naive oracle (AGENTS.md rule 3).
#include <catch2/catch_test_macros.hpp>

#include "movegen/movegen.hpp"
#include "variant/standard.hpp"

using namespace cb;

TEST_CASE("standard chess perft from the initial position", "[perft][standard]") {
  const auto v = makeStandardChess();
  REQUIRE(v.has_value());
  Position p = Position::startPosition(*v);
  const MoveGen gen(*v);

  REQUIRE(gen.perft(p, 1) == 20);
  REQUIRE(gen.perft(p, 2) == 400);
  REQUIRE(gen.perft(p, 3) == 8902);
  REQUIRE(gen.perft(p, 4) == 197281);
}

TEST_CASE("standard chess perft is deeper than tactics can hide in", "[perft][slow]") {
  const auto v = makeStandardChess();
  Position p = Position::startPosition(*v);
  const MoveGen gen(*v);
  REQUIRE(gen.perft(p, 5) == 4865609);
}
