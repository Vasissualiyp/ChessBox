// SPDX-License-Identifier: GPL-3.0-or-later
// A known limitation, written down as an executable test so it cannot be
// forgotten or rediscovered as a mystery.
//
// When one step leaves the board on two axes at once, the fold applies the seam
// transforms in axis order. For most gluings that is unambiguous - a torus and a
// Klein bottle both fold the same either way, because leaving on the min face and
// leaving on the max face use inverse transforms. But two seams whose transforms
// touch a shared axis in incompatible ways give different answers depending on the
// order, and then retracing a ray from its destination does not return along the
// path it came.
//
// Consequences, both already reflected in the code:
//   - the canonical order (lowest axis index first) is part of the contract;
//   - MoveGen::isAttacked uses a forward scan on any glued board, because the
//     backward walk it uses on a box would be unsound here.
#include <catch2/catch_test_macros.hpp>

#include "geometry/geometry.hpp"
#include "support/geometry_helpers.hpp"

using namespace cb;
using namespace cb::test;

namespace {

/// Walk one step forward, then one step back along the reversed direction, and
/// report whether the ray came home.
bool stepIsReversible(const Geometry& g, CellId from, const Direction& d) {
  Walker out = g.start(from, d);
  if (!g.step(out)) return true;  // a ray that ends cannot disagree about anything
  Walker back = g.start(out.cell, out.dir.negated());
  if (!g.step(back)) return false;
  return back.cell == from;
}

}  // namespace

TEST_CASE("stepping is reversible on the standard topologies", "[unit][geometry]") {
  const DimSpec d2 = makeDims({6, 6});
  IdentDecl px;
  px.axis = 0;
  px.kind = BoundaryKind::Periodic;
  IdentDecl py;
  py.axis = 1;
  py.kind = BoundaryKind::Periodic;
  IdentDecl pyFlip = py;
  pyFlip.flipAxes.push_back(0);
  IdentDecl mirrorX;
  mirrorX.axis = 0;
  mirrorX.kind = BoundaryKind::Mirror;

  struct Case {
    const char* name;
    std::vector<IdentDecl> idents;
  };
  const std::vector<Case> cases{{"torus", {px, py}},
                                {"cylinder", {px}},
                                {"klein", {px, pyFlip}},
                                {"moebius", {pyFlip}},
                                {"mirror", {mirrorX}}};

  for (const Case& c : cases) {
    const Geometry g = Geometry::create(d2, c.idents).value();
    for (CellId cell = 0; cell < d2.cellCount(); ++cell) {
      for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
          if (dx == 0 && dy == 0) continue;
          CAPTURE(c.name, cell, dx, dy);
          REQUIRE(stepIsReversible(g, cell, dir(d2, {dx, dy})));
        }
      }
    }
  }
}

TEST_CASE("a corner fold can be order-dependent, and we know it", "[unit][geometry]") {
  // Axis 1 is glued periodically with a flip of axis 2, while axis 2 has a mirror
  // wall. A step that leaves on axis 1 and axis 2 together therefore has two
  // transforms to apply whose effects on axis 2 disagree, and the answer depends on
  // which is applied first.
  const DimSpec d = makeDims({4, 5, 5});
  IdentDecl glue;
  glue.axis = 1;
  glue.kind = BoundaryKind::Periodic;
  glue.flipAxes.push_back(2);
  IdentDecl wall;
  wall.axis = 2;
  wall.side = Side::Max;
  wall.kind = BoundaryKind::Mirror;
  const Geometry g = Geometry::create(d, std::vector<IdentDecl>{glue, wall}).value();

  // Find a step for which stepping forward and then back does not come home. Its
  // existence is the limitation; if a future change to the fold removes it, this
  // test fails and the forward-scan fallback in MoveGen::isAttacked can be revisited.
  bool foundIrreversible = false;
  for (CellId cell = 0; cell < d.cellCount() && !foundIrreversible; ++cell) {
    for (int dy = -2; dy <= 2 && !foundIrreversible; ++dy) {
      for (int dz = -2; dz <= 2 && !foundIrreversible; ++dz) {
        if (dy == 0 && dz == 0) continue;
        if (!stepIsReversible(g, cell, dir(d, {0, dy, dz}))) foundIrreversible = true;
      }
    }
  }
  REQUIRE(foundIrreversible);
}
