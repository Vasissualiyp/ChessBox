// SPDX-License-Identifier: GPL-3.0-or-later
//
// A piece's body as data. The properties worth pinning are the ones an author can get
// wrong and a renderer cannot recover from: a shape that crosses itself, a foot that
// does not touch the board, an element on a piece that has no room for one.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "assets/piece_model.hpp"

using namespace cb;
using namespace cb::assets;

namespace {

PieceModel goodModel() {
  return archetypeModel("dome");
}

}  // namespace

TEST_CASE("every shipped archetype has an editable model", "[unit]") {
  // The invariant the fallback exists for: a piece is never un-editable, and never
  // unplayable, for want of an asset.
  for (const char* name : {"dome", "tower", "wedge", "spire", "crown", "monolith", "horn",
                           "nothing-anyone-has-shipped"}) {
    INFO("archetype " << name);
    const PieceModel m = archetypeModel(name);
    CHECK(m.profile.points.size() >= 3);
    const Result<void> ok = validate(m);
    const std::string why = ok.has_value() ? std::string{} : ok.error().format();
    INFO(why);
    CHECK(ok.has_value());
  }
}

TEST_CASE("a profile that crosses itself is refused, not drawn badly", "[unit]") {
  PieceModel m = goodModel();
  // A bow tie: two triangles joined at a crossing, which has no inside to sweep.
  m.profile.points = {{0, 0}, {400, 0}, {0, 400}, {400, 400}};
  const Result<void> ok = validate(m);
  REQUIRE_FALSE(ok.has_value());
  CHECK(ok.error().message.find("crosses itself") != std::string::npos);
}

TEST_CASE("a profile must stand on the board", "[unit]") {
  PieceModel m = goodModel();
  for (ModelPoint& p : m.profile.points) p.y = static_cast<std::int16_t>(p.y + 50);
  const Result<void> ok = validate(m);
  REQUIRE_FALSE(ok.has_value());
  CHECK(ok.error().message.find("height 0") != std::string::npos);
}

TEST_CASE("a profile may not leave its box", "[unit]") {
  PieceModel m = goodModel();
  m.profile.points.push_back({1400, 200});
  CHECK_FALSE(validate(m).has_value());

  // Radius is a distance from the axis. A negative one sweeps the solid through itself.
  PieceModel n = goodModel();
  n.profile.points[1].x = -50;
  CHECK_FALSE(validate(n).has_value());
}

TEST_CASE("a symmetry says how many copies of an element it places", "[unit]") {
  CHECK(copiesOf(Symmetry{SymmetryKind::Mirror, 2}) == 2);
  CHECK(copiesOf(Symmetry{SymmetryKind::KFold, 4}) == 4);
  CHECK(copiesOf(Symmetry{SymmetryKind::KFold, 7}) == 7);
  // A radially symmetric piece places none - there is no orientation to place one at.
  CHECK(copiesOf(Symmetry{SymmetryKind::Full, 1}) == 0);
  // And a degenerate k is none rather than one, so a half-finished picker cannot
  // produce a lopsided piece.
  CHECK(copiesOf(Symmetry{SymmetryKind::KFold, 1}) == 0);
}

TEST_CASE("a radially symmetric piece cannot carry an element", "[unit]") {
  PieceModel m = goodModel();
  REQUIRE(m.symmetry.kind == SymmetryKind::Full);
  Element e;
  e.outline.points = {{0, 0}, {100, 0}, {100, 100}};
  m.elements.push_back(e);
  const Result<void> ok = validate(m);
  REQUIRE_FALSE(ok.has_value());
  // And it says why, because it is a thing the author asked for on purpose.
  CHECK(ok.error().message.find("k-fold or mirror") != std::string::npos);
}

TEST_CASE("the knight and the queen carry the elements their shape needs", "[unit]") {
  const PieceModel knight = archetypeModel("wedge");
  CHECK(knight.symmetry.kind == SymmetryKind::Mirror);
  CHECK(knight.elements.size() == 1);
  CHECK(copiesOf(knight.symmetry) == 2);  // a pair of ears from one

  const PieceModel queen = archetypeModel("crown");
  CHECK(queen.symmetry.kind == SymmetryKind::KFold);
  CHECK(queen.elements.size() == 1);
  CHECK(copiesOf(queen.symmetry) == 5);  // a ring of points from one
}

TEST_CASE("a segment count outside the buildable range is refused", "[unit]") {
  PieceModel m = goodModel();
  m.segments = 2;
  CHECK_FALSE(validate(m).has_value());
  m.segments = 200;
  CHECK_FALSE(validate(m).has_value());
  m.segments = 3;
  CHECK(validate(m).has_value());
}

TEST_CASE("an icon draft comes back from a profile, and it is valid", "[unit]") {
  for (const char* name : {"dome", "tower", "crown", "horn"}) {
    INFO("archetype " << name);
    const PieceModel m = archetypeModel(name);
    const IconModel icon = iconFromProfile(m);
    REQUIRE(icon.fills.size() == 1);
    // Both halves of the silhouette, so the draft is a figure and not a half-figure -
    // less the points that sit on the axis, which mirror onto themselves.
    CHECK(icon.fills[0].points.size() > m.profile.points.size());
    CHECK(icon.fills[0].points.size() <= m.profile.points.size() * 2);
    const Result<void> ok = validate(icon);
    const std::string why = ok.has_value() ? std::string{} : ok.error().format();
    INFO(why);
    CHECK(ok.has_value());
  }
}

TEST_CASE("an icon with no outline is refused", "[unit]") {
  const IconModel icon;
  CHECK_FALSE(validate(icon).has_value());
}
