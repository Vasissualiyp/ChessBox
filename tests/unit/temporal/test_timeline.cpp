// SPDX-License-Identifier: GPL-3.0-or-later
//
// M6.2 (first principles): the multiverse's timeline activity and present line, from the
// reference game's written rules. No positions and no moves here - just which boards
// exist, which timelines are active, and where the present line falls. These are the
// predicates every other temporal policy is written against.
#include <catch2/catch_test_macros.hpp>

#include "temporal/timeline.hpp"

using namespace cb;
using namespace cb::temporal;

TEST_CASE("a fresh game has one active original timeline", "[unit][temporal]") {
  const TimelineModel m;
  REQUIRE(m.timelines().size() == 1);
  CHECK(m.timelines()[0].id == 0);
  CHECK(m.active(0));
  CHECK(m.presentTurn() == -1);  // no boards yet
  CHECK(m.boardsInPresent().empty());
}

TEST_CASE("a timeline is playable where its latest board is", "[unit][temporal]") {
  TimelineModel m;
  REQUIRE(m.addBoard(0, 0));
  REQUIRE(m.addBoard(0, 1));
  REQUIRE(m.addBoard(0, 2));
  CHECK_FALSE(m.addBoard(0, 2));  // a board already lives there
  const Timeline* t = m.find(0);
  REQUIRE(t != nullptr);
  CHECK(t->latest() == 2);
  CHECK(t->hasBoard(0));
  CHECK_FALSE(t->hasBoard(9));
}

TEST_CASE("a player's nth branch is active only once the opponent has branched",
          "[unit][temporal]") {
  TimelineModel m;
  // White opens a timeline; with Black on zero branches, the first is active.
  const auto w1 = m.branch(BranchOwner::White, 0, 0);
  REQUIRE(w1.has_value());
  CHECK(*w1 == -1);  // White branches down from the origin
  CHECK(m.active(*w1));

  // White's second is active only once Black has created at least one.
  const auto w2 = m.branch(BranchOwner::White, 0, 0);
  REQUIRE(w2.has_value());
  CHECK(*w2 == -2);
  CHECK_FALSE(m.active(*w2));

  const auto b1 = m.branch(BranchOwner::Black, 0, 0);
  REQUIRE(b1.has_value());
  CHECK(*b1 == 1);
  CHECK(m.active(*b1));  // Black's first: White has branched
  CHECK(m.active(*w2));  // now White's second counts too
  CHECK_FALSE(m.active(static_cast<LineId>(99)));
}

TEST_CASE("branching takes the next free index", "[unit][temporal]") {
  TimelineModel m;
  const auto w1 = m.branch(BranchOwner::White, 0, 0);
  const auto w2 = m.branch(BranchOwner::White, 0, 0);
  REQUIRE(w1.has_value());
  REQUIRE(w2.has_value());
  CHECK(*w1 == -1);
  CHECK(*w2 == -2);
  const auto b1 = m.branch(BranchOwner::Black, 0, 0);
  REQUIRE(b1.has_value());
  CHECK(*b1 == 1);
  CHECK_FALSE(m.branch(BranchOwner::Original, 0, 0).has_value());
}

TEST_CASE("the present line sits on the leftmost active board", "[unit][temporal]") {
  TimelineModel m;
  REQUIRE(m.addBoard(0, 0));
  REQUIRE(m.addBoard(0, 1));
  REQUIRE(m.addBoard(0, 2));  // original is furthest right
  const auto w1 = m.branch(BranchOwner::White, 0, 0);
  REQUIRE(w1.has_value());
  REQUIRE(m.addBoard(*w1, 0));  // the branch starts back at turn 0

  // The furthest-left active board is the branch at turn 0, so that is the present.
  CHECK(m.presentTurn() == 0);
  const auto present = m.boardsInPresent();
  REQUIRE(present.size() == 2);  // original and branch both have a turn-0 board
  CHECK(present[0].turn == 0);
  CHECK(present[1].turn == 0);

  // Advancing the branch moves the present forward.
  REQUIRE(m.addBoard(*w1, 1));
  CHECK(m.presentTurn() == 1);

  // An inactive timeline contributes no board to the present.
  const auto w2 = m.branch(BranchOwner::White, 0, 0);  // White's second, inactive
  REQUIRE(w2.has_value());
  REQUIRE(m.addBoard(*w2, 0));
  for (const BoardRef& b : m.boardsInPresent()) CHECK(b.line != *w2);
}
