// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include "movegen/movegen.hpp"
#include "variant/standard.hpp"

using namespace cb;
using namespace cb::view;

TEST_CASE("a snapshot is genuinely isolated from the live position", "[unit][view]") {
  // The renderer must never see a half-applied move, and the engine must never have to
  // wait for the GPU. That is only true if the snapshot is a copy - so this test plays
  // a long sequence of moves and asserts the snapshot did not move with it.
  const auto v = makeStandardChess();
  Position p = Position::startPosition(*v);
  const PositionView snap = PositionView::capture(p);
  const std::uint64_t hashAtCapture = snap.hash();

  const MoveGen gen(*v);
  MoveGen::Buffers bufs;
  bufs.ensure(1, v->moveUpperBound());
  std::vector<std::pair<Move, Undo>> played;
  for (int i = 0; i < 1000; ++i) {
    MoveList moves(v->moveUpperBound());
    gen.generateLegal(p, moves);
    if (moves.empty()) break;
    Undo u;
    const Move m = moves[static_cast<std::size_t>(i) % moves.size()];
    p.make(m, u);
    played.emplace_back(m, u);
  }
  REQUIRE(played.size() > 10);
  REQUIRE(p.hash() != hashAtCapture);  // the position really did move on

  REQUIRE(snap.hash() == hashAtCapture);
  REQUIRE(snap.sideToMove() == Color::White);
  const Position fresh = Position::startPosition(*v);
  for (CellId c = 0; c < snap.cellCount(); ++c) REQUIRE(snap.at(c) == fresh.at(c));
}

TEST_CASE("a snapshot carries the UI's highlight state", "[unit][view]") {
  // Highlights live in the snapshot so that a frame has one source of truth: the
  // renderer cannot disagree with the engine about which moves are legal.
  const auto v = makeStandardChess();
  Position p = Position::startPosition(*v);
  const MoveGen gen(*v);
  MoveList moves(v->moveUpperBound());
  gen.generateLegal(p, moves);

  PositionView snap = PositionView::capture(p);
  const CellId from = moves[0].from;
  std::vector<CellId> targets;
  for (const Move& m : moves) {
    if (m.from == from) targets.push_back(m.to);
  }
  snap.setSelected(from);
  snap.setHighlighted(targets);

  REQUIRE(snap.selected() == from);
  REQUIRE(snap.highlighted().size() == targets.size());
  REQUIRE_FALSE(snap.highlighted().empty());
}
