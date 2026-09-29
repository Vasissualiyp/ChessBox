// SPDX-License-Identifier: GPL-3.0-or-later
//
// M6.2: a timeline remembers the board it was born from. The renderer draws the
// multiverse's shape from this, so the relationship has to survive the branch - it is
// engine state, tested with no renderer in sight.
#include <catch2/catch_test_macros.hpp>

#include "temporal/multiverse.hpp"

using namespace cb;
using namespace cb::temporal;

TEST_CASE("branching records the board it came from", "[unit][temporal]") {
  TemporalPolicy policy;
  policy.originLine = 2;
  Multiverse m(policy, 6, 4);
  const BoardKey start = m.origin();

  REQUIRE(m.apply(start, start).has_value());    // (1,2)
  REQUIRE(m.apply({1, 2}, {1, 2}).has_value());  // (2,2)
  const auto first = m.apply({2, 2}, start);     // White branches from (0,2)
  REQUIRE(first.has_value());
  const BoardKey branch = (*first)[1];  // (1,1)

  const Timeline* t = m.timelines().find(branch.line);
  REQUIRE(t != nullptr);
  CHECK(t->parentLine == start.line);  // 2
  CHECK(t->parentTurn == start.turn);  // 0

  // A second branch records its own parent. The playable present board is the first
  // branch; Black travels from it into the past and opens a second timeline.
  const auto second = m.apply(branch, start);
  REQUIRE(second.has_value());
  const BoardKey branch2 = (*second)[1];
  const Timeline* t2 = m.timelines().find(branch2.line);
  REQUIRE(t2 != nullptr);
  CHECK(t2->parentLine == start.line);
  CHECK(t2->parentTurn == start.turn);

  // The original timeline came from nowhere.
  const Timeline* original = m.timelines().find(start.line);
  REQUIRE(original != nullptr);
  CHECK(original->parentTurn < 0);
}
