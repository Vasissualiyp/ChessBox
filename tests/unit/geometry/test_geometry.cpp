// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/geometry.hpp"

#include <catch2/catch_test_macros.hpp>

#include "support/geometry_helpers.hpp"

using namespace cb;
using namespace cb::test;

namespace {

Geometry box2(int w, int h) {
  return Geometry::create(makeDims({w, h}), {}).value();
}

IdentDecl periodic(std::uint8_t axis) {
  IdentDecl d;
  d.axis = axis;
  d.kind = BoundaryKind::Periodic;
  return d;
}

IdentDecl periodicWithFlip(std::uint8_t axis, std::uint8_t flip) {
  IdentDecl d = periodic(axis);
  d.flipAxes.push_back(flip);
  return d;
}

IdentDecl mirror(std::uint8_t axis, Side side) {
  IdentDecl d;
  d.axis = axis;
  d.side = side;
  d.kind = BoundaryKind::Mirror;
  return d;
}

Geometry make(const DimSpec& d, std::vector<IdentDecl> idents) {
  auto g = Geometry::create(d, idents);
  REQUIRE(g.has_value());
  return std::move(*g);
}

}  // namespace

// --------------------------------------------------------------------------
// Box: the M1 baseline
// --------------------------------------------------------------------------

TEST_CASE("a box board terminates rays at its walls", "[unit][geometry]") {
  const Geometry g = box2(8, 8);
  REQUIRE(g.isBox());
  REQUIRE(g.isOrientable());
  const DimSpec& d = g.dims();
  const Direction east = dir(d, {1, 0});
  const Orbit o = walk(g, d.toCell(Coord::of({0, 3})), east);
  REQUIRE_FALSE(o.closed);
  REQUIRE(o.cells.size() == 7);  // a1-side to the far wall
  REQUIRE(o.cells.back() == d.toCell(Coord::of({7, 3})));
}

TEST_CASE("a face reports whether it glues, reflects, or is open", "[unit][geometry]") {
  // The move animation tells a portal from a bounce by asking this, so it has to be
  // exact: an open face ends the ray, a mirror turns it, a periodic face relocates it.
  const Geometry g = make(makeDims({8, 8}), {periodic(0), mirror(1, Side::Max)});
  CHECK(g.boundaryKind(0, Side::Min) == BoundaryKind::Periodic);
  CHECK(g.boundaryKind(0, Side::Max) == BoundaryKind::Periodic);
  CHECK(g.boundaryKind(1, Side::Max) == BoundaryKind::Mirror);
  CHECK(g.boundaryKind(1, Side::Min) == BoundaryKind::Open);
  CHECK(box2(8, 8).boundaryKind(0, Side::Min) == BoundaryKind::Open);
}

TEST_CASE("interior steps are plain integer adds", "[unit][geometry]") {
  const Geometry g = box2(8, 8);
  const DimSpec& d = g.dims();
  const Direction ne = dir(d, {1, 1});
  Walker w = g.start(d.toCell(Coord::of({2, 2})), ne);
  const std::int32_t delta = w.delta;
  REQUIRE(g.step(w));
  REQUIRE(w.cell == d.toCell(Coord::of({3, 3})));
  REQUIRE(w.delta == delta);  // an interior step never re-derives the direction
  REQUIRE(w.coord == Coord::of({3, 3}));
}

TEST_CASE("a step off the board leaves the walker untouched", "[unit][geometry]") {
  // A partially-applied step would corrupt the ray; the interior test checks all
  // axes before mutating any (ARCH section 4.2).
  const Geometry g = box2(8, 8);
  const DimSpec& d = g.dims();
  const Direction ne = dir(d, {1, 1});
  Walker w = g.start(d.toCell(Coord::of({7, 3})), ne);
  const Walker before = w;
  REQUIRE_FALSE(g.step(w));
  REQUIRE(w.cell == before.cell);
  REQUIRE(w.coord == before.coord);
}

// --------------------------------------------------------------------------
// Cylinder and torus
// --------------------------------------------------------------------------

TEST_CASE("a cylinder wraps one axis and keeps the direction", "[unit][geometry]") {
  const DimSpec d = makeDims({8, 8});
  const Geometry g = make(d, {periodic(0)});
  REQUIRE_FALSE(g.isBox());
  REQUIRE(g.isOrientable());

  // A rook's orbit along a wrapped rank closes after exactly the extent.
  const Orbit o = walk(g, d.toCell(Coord::of({0, 3})), dir(d, {1, 0}));
  REQUIRE(o.closed);
  REQUIRE(o.cells.size() == 7);

  // The unwrapped axis still has walls.
  const Orbit up = walk(g, d.toCell(Coord::of({0, 0})), dir(d, {0, 1}));
  REQUIRE_FALSE(up.closed);
  REQUIRE(up.cells.size() == 7);
}

TEST_CASE("a torus gives every cell the same neighbourhood", "[unit][geometry]") {
  const DimSpec d = makeDims({5, 5});
  const Geometry g = make(d, {periodic(0), periodic(1)});

  const std::vector<Direction> kingDirs{dir(d, {1, 0}),  dir(d, {-1, 0}), dir(d, {0, 1}),
                                        dir(d, {0, -1}), dir(d, {1, 1}),  dir(d, {1, -1}),
                                        dir(d, {-1, 1}), dir(d, {-1, -1})};
  for (CellId c = 0; c < d.cellCount(); ++c) {
    std::set<CellId> n;
    for (const Direction& dd : kingDirs) {
      Walker w = g.start(c, dd);
      REQUIRE(g.step(w));  // no walls anywhere on a torus
      n.insert(w.cell);
    }
    REQUIRE(n.size() == 8);
  }
}

TEST_CASE("a diagonal step across a torus corner crosses two seams", "[unit][geometry]") {
  const DimSpec d = makeDims({5, 5});
  const Geometry g = make(d, {periodic(0), periodic(1)});
  Walker w = g.start(d.toCell(Coord::of({4, 4})), dir(d, {1, 1}));
  REQUIRE(g.step(w));
  REQUIRE(w.coord == Coord::of({0, 0}));
}

TEST_CASE("an N-dimensional torus is uniform in every dimension", "[unit][geometry]") {
  for (int n = 3; n <= 6; ++n) {
    std::vector<AxisDecl> axes;
    for (int i = 0; i < n; ++i) {
      axes.push_back(AxisDecl{3, AxisKind::Spatial, "a" + std::to_string(i)});
    }
    const DimSpec d = DimSpec::create(axes).value();
    std::vector<IdentDecl> ids;
    for (int i = 0; i < n; ++i) ids.push_back(periodic(static_cast<std::uint8_t>(i)));
    const Geometry g = make(d, ids);

    for (CellId c = 0; c < d.cellCount(); ++c) {
      for (int a = 0; a < n; ++a) {
        std::array<std::int16_t, kMaxDims> v{};
        v[static_cast<std::size_t>(a)] = 1;
        Walker w = g.start(c, Direction::make(v, static_cast<std::uint8_t>(n)));
        REQUIRE(g.step(w));  // never a wall
      }
    }
  }
}

// --------------------------------------------------------------------------
// Mirrors
// --------------------------------------------------------------------------

TEST_CASE("a mirror wall reflects the ray and flips its direction", "[unit][geometry]") {
  const DimSpec d = makeDims({8, 8});
  const Geometry g = make(d, {mirror(0, Side::Max)});

  // A reflection reverses handedness but glues nothing, so the surface stays
  // orientable - the two notions are deliberately separate.
  REQUIRE(g.isOrientable());
  REQUIRE(g.hasOrientationReversingFace());

  Walker w = g.start(d.toCell(Coord::of({7, 3})), dir(d, {1, 0}));
  REQUIRE(g.step(w));
  REQUIRE(w.coord == Coord::of({7, 3}));  // reflected back onto the same cell
  REQUIRE(w.dir == dir(d, {-1, 0}));      // and now travelling the other way

  // A ray's total path length matches the unfolded reflection: 7 steps east from
  // x=0 reaches the wall, then it comes back.
  const Orbit o = walk(g, d.toCell(Coord::of({0, 3})), dir(d, {1, 0}));
  REQUIRE(o.cells.size() >= 8);
  REQUIRE(o.cells[7] == d.toCell(Coord::of({7, 3})));
  REQUIRE(o.cells[8] == d.toCell(Coord::of({6, 3})));
}

TEST_CASE("a diagonal ray in a mirrored box bounces like light", "[unit][geometry]") {
  const DimSpec d = makeDims({4, 4});
  const Geometry g = make(d, {mirror(0, Side::Max), mirror(0, Side::Min)});
  // Starting at (0,0) going north-east, the ray bounces off x=3 and continues.
  const Orbit o = walk(g, d.toCell(Coord::of({0, 0})), dir(d, {1, 1}), 16);
  REQUIRE(o.cells[0] == d.toCell(Coord::of({1, 1})));
  REQUIRE(o.cells[1] == d.toCell(Coord::of({2, 2})));
  REQUIRE(o.cells[2] == d.toCell(Coord::of({3, 3})));
  REQUIRE(o.cells.size() == 3);  // y=3 is an open wall, so the ray ends there
}

// --------------------------------------------------------------------------
// Non-orientable surfaces - the sharp tests
// --------------------------------------------------------------------------

TEST_CASE("a Moebius band reverses orientation after one circuit", "[unit][geometry]") {
  const DimSpec d = makeDims({8, 4});
  // x is periodic, and crossing that seam flips y: the classic Moebius gluing.
  const Geometry g = make(d, {periodicWithFlip(0, 1)});
  REQUIRE_FALSE(g.isOrientable());

  // A rook along the wrapped axis returns to its own cell after one circuit only
  // if its row is the fixed row of the flip; otherwise it lands on the mirrored
  // row and needs a second circuit.
  const CellId start = d.toCell(Coord::of({0, 0}));
  Walker w = g.start(start, dir(d, {1, 0}));
  for (int i = 0; i < 8; ++i) REQUIRE(g.step(w));
  REQUIRE(w.coord == Coord::of({0, 3}));  // y flipped from 0 to 3
  for (int i = 0; i < 8; ++i) REQUIRE(g.step(w));
  REQUIRE(w.coord == Coord::of({0, 0}));  // back home after two circuits
}

TEST_CASE("a Moebius seam flips a diagonal ray's direction", "[unit][geometry]") {
  const DimSpec d = makeDims({6, 6});
  const Geometry g = make(d, {periodicWithFlip(0, 1)});
  Walker w = g.start(d.toCell(Coord::of({5, 2})), dir(d, {1, 1}));
  REQUIRE(g.step(w));
  // x wraps 5 -> 0; y is flipped (2+1=3 -> 6-1-3=2) and the y-direction reverses.
  REQUIRE(w.coord[0] == 0);
  REQUIRE(w.dir.v[1] == -1);
}

TEST_CASE("a Klein bottle destroys bishop colour binding", "[unit][geometry]") {
  // On any orientable board a bishop is confined to one colour. A Klein bottle
  // admits no global two-colouring, so the bishop's reachable set is the entire
  // board. That is a mathematical fingerprint of the surface and a far sharper
  // test than any hand-written move list (M3.3).
  const DimSpec d = makeDims({6, 6});
  const Geometry klein = make(d, {periodic(0), periodicWithFlip(1, 0)});
  REQUIRE_FALSE(klein.isOrientable());

  const std::vector<Direction> diagonals{dir(d, {1, 1}), dir(d, {1, -1}), dir(d, {-1, 1}),
                                         dir(d, {-1, -1})};
  const auto reach = reachable(klein, d.toCell(Coord::of({0, 0})), diagonals);
  REQUIRE(reach.size() == d.cellCount());

  // The control: on a plain torus the bishop still sees exactly half the board.
  const Geometry torus = make(d, {periodic(0), periodic(1)});
  REQUIRE(torus.isOrientable());
  const auto torusReach = reachable(torus, d.toCell(Coord::of({0, 0})), diagonals);
  REQUIRE(torusReach.size() == d.cellCount() / 2);
}

TEST_CASE("a 3-D Klein geometry composes wraps and flips", "[unit][geometry]") {
  const DimSpec d = makeDims({4, 4, 4});
  const Geometry g = make(d, {periodic(0), periodicWithFlip(1, 0), periodic(2)});
  REQUIRE_FALSE(g.isOrientable());
  // Every face is glued, so no ray in any of the 26 king directions ever ends.
  for (CellId c = 0; c < d.cellCount(); ++c) {
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -1; dz <= 1; ++dz) {
          if (dx == 0 && dy == 0 && dz == 0) continue;
          Walker w = g.start(c, dir(d, {dx, dy, dz}));
          REQUIRE(g.step(w));
          REQUIRE(w.cell < d.cellCount());
        }
      }
    }
  }
}

TEST_CASE("mixed per-axis boundaries stay independent", "[unit][geometry]") {
  const DimSpec d = makeDims({5, 5, 5});
  // x periodic, y mirrored at its max face, z open.
  const Geometry g = make(d, {periodic(0), mirror(1, Side::Max)});
  Walker wx = g.start(d.toCell(Coord::of({4, 2, 2})), dir(d, {1, 0, 0}));
  REQUIRE(g.step(wx));
  REQUIRE(wx.coord == Coord::of({0, 2, 2}));

  Walker wy = g.start(d.toCell(Coord::of({2, 4, 2})), dir(d, {0, 1, 0}));
  REQUIRE(g.step(wy));
  REQUIRE(wy.coord == Coord::of({2, 4, 2}));
  REQUIRE(wy.dir.v[1] == -1);

  Walker wz = g.start(d.toCell(Coord::of({2, 2, 4})), dir(d, {0, 0, 1}));
  REQUIRE_FALSE(g.step(wz));  // z is open
}

// --------------------------------------------------------------------------
// Validation
// --------------------------------------------------------------------------

TEST_CASE("contradictory or malformed geometry is rejected", "[unit][geometry]") {
  const DimSpec d = makeDims({6, 6});

  SECTION("a face identified twice") {
    const auto r = Geometry::create(d, std::vector<IdentDecl>{periodic(0), periodic(0)});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().message.find("identified twice") != std::string::npos);
  }
  SECTION("periodic and mirror on the same face") {
    const auto r =
        Geometry::create(d, std::vector<IdentDecl>{periodic(0), mirror(0, Side::Max)});
    REQUIRE_FALSE(r.has_value());
  }
  SECTION("an axis that does not exist") {
    const auto r = Geometry::create(d, std::vector<IdentDecl>{periodic(5)});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::ValidationError);
  }
  SECTION("a flip of a nonexistent axis") {
    const auto r = Geometry::create(d, std::vector<IdentDecl>{periodicWithFlip(0, 4)});
    REQUIRE_FALSE(r.has_value());
  }
  SECTION("a swap of axes with unequal extents") {
    const DimSpec rect = makeDims({6, 4});
    IdentDecl bad = periodic(0);
    bad.swapA = 0;
    bad.swapB = 1;
    const auto r = Geometry::create(rect, std::vector<IdentDecl>{bad});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().message.find("equal extents") != std::string::npos);
  }
  SECTION("kind 'open' is not an identification") {
    IdentDecl bad;
    bad.kind = BoundaryKind::Open;
    const auto r = Geometry::create(d, std::vector<IdentDecl>{bad});
    REQUIRE_FALSE(r.has_value());
  }
}
