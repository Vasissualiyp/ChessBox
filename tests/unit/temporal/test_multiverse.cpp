// SPDX-License-Identifier: GPL-3.0-or-later
//
// M6.2: what a move does to the multiverse, under the reference policy. A move advances
// the source's timeline; a move to the frontier advances the destination's; a move into
// the past branches a fresh timeline one branch-advance on. Positions and move generation
// live a layer up.
#include <catch2/catch_test_macros.hpp>

#include "temporal/multiverse.hpp"

using namespace cb;
using namespace cb::temporal;

namespace {

/// A policy with the original timeline in the middle, so both players can branch.
TemporalPolicy centred(LineId origin, LineId maxCoord) {
  TemporalPolicy p;
  p.originLine = origin;
  p.whiteSign = -1;  // White down
  p.blackSign = 1;   // Black up
  p.branchAdvance = 1;
  (void)maxCoord;
  return p;
}

}  // namespace

TEST_CASE("a fresh multiverse is one board, White to move", "[unit][temporal]") {
  const Multiverse m(centred(2, 4), 5, 4);
  const BoardKey start = m.origin();
  CHECK(m.live().size() == 1);
  CHECK(m.exists(start));
  CHECK(m.playable(start));
  CHECK(m.sideToMove(start) == Color::White);

  const auto present = m.present();
  REQUIRE(present.size() == 1);
  CHECK(present[0] == start);
  CHECK(m.mover() == Color::White);
  CHECK(m.unansweredPresent().size() == 1);
}

TEST_CASE("a move inside a board advances that timeline by one turn",
          "[unit][temporal]") {
  Multiverse m(centred(2, 4), 5, 4);
  const BoardKey start = m.origin();
  const auto created = m.apply(start, start);
  REQUIRE(created.has_value());
  REQUIRE(created->size() == 1);
  CHECK((*created)[0] == BoardKey{1, start.line});
  CHECK(m.playable(BoardKey{1, start.line}));
  CHECK_FALSE(m.playable(start));

  const auto present = m.present();
  REQUIRE(present.size() == 1);
  CHECK(present[0] == BoardKey{1, start.line});
  CHECK(m.mover() == Color::Black);
  CHECK(m.unansweredPresent().size() == 1);
}

TEST_CASE("a move into the past branches a new timeline", "[unit][temporal]") {
  Multiverse m(centred(2, 4), 5, 4);
  const BoardKey start = m.origin();
  REQUIRE(m.apply(start, start).has_value());    // (1,2)
  REQUIRE(m.apply({1, 2}, {1, 2}).has_value());  // (2,2)

  // White sends a piece back to the starting board, which already has a future.
  const auto created = m.apply({2, 2}, start);
  REQUIRE(created.has_value());
  REQUIRE(created->size() == 2);
  CHECK((*created)[0] == BoardKey{3, 2});  // the source's timeline still advances

  // The new timeline is one branch-advance on from the target, and on White's side (a
  // lower line index, which is "down").
  const BoardKey branch = (*created)[1];
  CHECK(branch.line == 1);
  CHECK(branch.turn == 1);
  CHECK(m.exists(branch));
  CHECK(m.timelines().active(branch.line));

  CHECK(m.present().size() == 2);
}

TEST_CASE("a move onto the frontier of another timeline advances it too",
          "[unit][temporal]") {
  Multiverse m(centred(2, 4), 5, 4);
  const BoardKey start = m.origin();
  REQUIRE(m.apply(start, start).has_value());    // (1,2)
  REQUIRE(m.apply({1, 2}, {1, 2}).has_value());  // (2,2)
  const auto branched = m.apply({2, 2}, start);
  REQUIRE(branched.has_value());
  const BoardKey branch = (*branched)[1];  // (1,1), Black to move
  REQUIRE(m.sideToMove(branch) == Color::Black);

  const BoardKey frontier{3, 2};
  REQUIRE(m.playable(frontier));
  const auto created = m.apply(branch, frontier);
  REQUIRE(created.has_value());
  CHECK(created->size() == 2);
  CHECK((*created)[0] == BoardKey{2, branch.line});  // source line advances
  CHECK((*created)[1] == BoardKey{4, 2});            // destination frontier advances
}

TEST_CASE("a move one turn forward stays on the source's own future",
          "[unit][temporal]") {
  Multiverse m(centred(2, 4), 5, 4);
  const BoardKey start = m.origin();
  const auto created = m.apply(start, BoardKey{1, start.line});
  REQUIRE(created.has_value());
  REQUIRE(created->size() == 1);
  CHECK((*created)[0] == BoardKey{1, start.line});
  CHECK(m.timelines().timelines().size() == 1);  // no new timeline

  REQUIRE(m.apply({1, start.line}, {1, start.line}).has_value());  // (2,line)
  CHECK_FALSE(m.apply({2, start.line}, {4, start.line}).has_value());
}

TEST_CASE("the multiverse refuses to move from a past board", "[unit][temporal]") {
  Multiverse m(centred(2, 4), 5, 4);
  const BoardKey start = m.origin();
  REQUIRE(m.apply(start, start).has_value());
  CHECK_FALSE(m.apply(start, start).has_value());
}

TEST_CASE("branch direction and capacity come from the policy", "[unit][temporal]") {
  TemporalPolicy p = centred(2, 4);
  p.whiteSign = 1;  // a variant could send White up
  p.blackSign = -1;
  Multiverse m(p, 5, 4);
  const BoardKey start = m.origin();
  REQUIRE(m.apply(start, start).has_value());    // (1,2)
  REQUIRE(m.apply({1, 2}, {1, 2}).has_value());  // (2,2)
  const auto branched = m.apply({2, 2}, start);
  REQUIRE(branched.has_value());
  CHECK((*branched)[1].line == 3);  // White up: origin + 1

  // With no room on White's side, a branch is refused and nothing changes.
  Multiverse none(centred(0, 0), 6, 0);
  const BoardKey s0 = none.origin();
  REQUIRE(none.apply(s0, s0).has_value());
  REQUIRE(none.apply({1, 0}, {1, 0}).has_value());
  const std::size_t before = none.live().size();
  CHECK_FALSE(none.apply({2, 0}, s0).has_value());
  CHECK(none.live().size() == before);
}
