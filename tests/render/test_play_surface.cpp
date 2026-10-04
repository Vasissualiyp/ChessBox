// SPDX-License-Identifier: GPL-3.0-or-later
//
// The play board as its own shape (M17), as geometry. No GPU: a tile's seat, its turn
// and its size are arithmetic, and that is exactly where the first cut of this went
// wrong - the tiles were placed on the surface but spun to whatever the shortest arc
// onto the normal happened to be, and sized as though the board were still flat.
//
// The claims here are the ones a player can see: a tile lies *along* the surface's own
// axes, a cell is as big as the step to its neighbour, the seams the variant declares
// close, the shape stands up in the board's own world, and the eversion turns the
// surface through itself without moving a cell off it.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#ifdef CB_HAVE_IMGUI

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "render/overture_scene.hpp"
#include "render/play_surface.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::render;
using Catch::Matchers::WithinAbs;

namespace {

float dist(const view::Vec3& a, const view::Vec3& b) {
  return view::length(a - b);
}

/// Rotate a vector by a unit quaternion (x, y, z, w) - the same arithmetic the vertex
/// shader applies, so what the test checks is what the card draws.
view::Vec3 byQuat(const std::array<float, 4>& q, const view::Vec3& v) {
  const view::Vec3 qv{q[0], q[1], q[2]};
  const view::Vec3 t = view::cross(qv, v) * 2.0f;
  return v + t * q[3] + view::cross(qv, t);
}

const SurfaceSeat& seatAt(const PlaySurface& s, const VariantSpec& v, int f, int r) {
  const CellId want = v.dims.toCell(Coord::of({f, r}));
  for (const SurfaceSeat& t : s.seats()) {
    if (t.cell == want) return t;
  }
  throw std::runtime_error("no such cell on the surface");
}

const SurfacePatch& patchAt(const PlaySurface& s, const VariantSpec& v, int f, int r) {
  const CellId want = v.dims.toCell(Coord::of({f, r}));
  for (const SurfacePatch& t : s.patches()) {
    if (t.cell == want) return t;
  }
  throw std::runtime_error("no such cell on the surface");
}

/// A patch corner, by its grid position.
view::Vec3 cornerOf(const SurfacePatch& p, int i, int j) {
  return p.pos[static_cast<std::size_t>(i * (kSurfaceSubdiv + 1) + j)];
}

const std::vector<std::string> kShapes{"cylinder", "torus", "mobius", "klein"};

/// How closely a seat's own +X may be expected to point at the next square along.
///
/// On a gently curved surface those are the same direction to a degree or two. A Klein
/// bottle's figure-eight cross-section is not gently curved: eight squares have to get
/// all the way round a lemniscate, which turns through more than a full circle *twice*,
/// and the chord to the next square leaves the tangent plane as it goes. There the claim
/// is only that the seat faces along the files rather than against them - the bug this
/// exists for put them at arbitrary angles, backwards among them.
float alongTolerance(const std::string& name) {
  return name == "klein" ? 0.0f : 0.9f;
}

}  // namespace

TEST_CASE("a surface tile lies along the surface's own axes", "[render]") {
  // The bug this pins: a quaternion that only takes local +Z onto the normal leaves the
  // tile free to spin in its own plane, and a board of squares turned to random angles
  // is not a board. The frame is the surface's: +X along the files, +Y along the ranks,
  // +Z out of it, right-handed - so a tile's edges run along the board's own lines.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    REQUIRE_FALSE(s.empty());
    CAPTURE(name);
    for (const SurfaceSeat& t : s.seats()) {
      const view::Vec3 ex = byQuat(t.quat, {1, 0, 0});
      const view::Vec3 ey = byQuat(t.quat, {0, 1, 0});
      const view::Vec3 ez = byQuat(t.quat, {0, 0, 1});
      // Orthonormal and right-handed: a rotation, never a squash or a mirror.
      CHECK_THAT(view::length(ex), WithinAbs(1.0f, 1e-3f));
      CHECK_THAT(view::dot(ex, ey), WithinAbs(0.0f, 1e-3f));
      CHECK_THAT(view::dot(ex, ez), WithinAbs(0.0f, 1e-3f));
      CHECK(view::dot(view::cross(ex, ey), ez) > 0.99f);
      // And it is the *surface's* frame: +Z is the outward normal the tile was given.
      CHECK_THAT(dist(ez, t.normal), WithinAbs(0.0f, 1e-3f));
      // +X follows the files: stepping one cell along the file axis moves that way. The
      // Klein figure-eight's own chord reverses at its crossing, so there the claim is
      // the frame's *continuity* - it agrees with its neighbour rather than with the
      // chord (M17.18).
      const Coord co = v.dims.toCoord(t.cell);
      const int f =
          co.c[0] + 1 < static_cast<int>(v.dims.extent(0)) ? co.c[0] + 1 : co.c[0] - 1;
      if (name == "klein") {
        const view::Vec3 nextEx =
            byQuat(seatAt(s, v, f, co.c[1]).quat, view::Vec3{1, 0, 0});
        CHECK(view::dot(ex, nextEx) > 0.0f);
        continue;
      }
      const float sign = co.c[0] + 1 < static_cast<int>(v.dims.extent(0)) ? 1.0f : -1.0f;
      const view::Vec3 along =
          view::normalize((seatAt(s, v, f, co.c[1]).centre - t.centre) * sign);
      CHECK(view::dot(along, ex) > alongTolerance(name));
    }
  }
}

TEST_CASE("a square is a patch of the surface, and the patches tile it", "[render]") {
  // The thing that makes this a board rather than fish scales. A square is not a
  // rectangle turned to face the surface - a rectangle is tangent at one point and
  // overlaps its neighbour at the other end - but a patch cut from the surface itself.
  // So: every patch has the shape of the surface under it, and two patches either side of
  // a lattice edge come within a hair of each other without ever crossing.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    const int side = kSurfaceSubdiv + 1;
    CAPTURE(name);
    CHECK(s.patches().size() == s.seats().size());
    for (int f = 0; f + 1 < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        const SurfacePatch& a = patchAt(s, v, f, r);
        const SurfacePatch& b = patchAt(s, v, f + 1, r);
        const float step = seatAt(s, v, f, r).stepU;
        for (int j = 0; j < side; ++j) {
          // The far edge of one square and the near edge of the next: a gap, because a
          // board needs a line between its squares, but a thin one - and never a
          // crossing, which is what an overlap would be.
          const view::Vec3 lastA = cornerOf(a, side - 1, j);
          const view::Vec3 firstB = cornerOf(b, 0, j);
          const float gap = dist(lastA, firstB);
          CAPTURE(f, r, j, gap, step);
          CHECK(gap > 0.0f);
          CHECK(gap < 0.30f * step);
        }
      }
    }
    // And the patch really is curved where the surface is: its corners do not all lie in
    // one plane. A flat quad would pass the gap test above and still be the old bug.
    float worst = 0.0f;
    for (const SurfacePatch& patch : s.patches()) {
      const view::Vec3 n = patch.normal[0];
      const view::Vec3 o = patch.pos[0];
      for (const view::Vec3& c : patch.pos) {
        worst = std::max(worst, std::abs(view::dot(c - o, n)));
      }
    }
    CHECK(worst > 0.01f);
  }
}

TEST_CASE("a seat is sized by the squares around it", "[render]") {
  // The seat is only there to stand a piece on, and a piece has to fit its square. An
  // embedding stretches the board where it opens a hole and squeezes it where it closes
  // one, so the size is read from the distance to the neighbouring seats.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    CAPTURE(name);
    for (int f = 1; f + 1 < nx; ++f) {
      for (int r = 1; r + 1 < nz; ++r) {
        const SurfaceSeat& t = seatAt(s, v, f, r);
        const float du = 0.5f * (dist(t.centre, seatAt(s, v, f + 1, r).centre) +
                                 dist(t.centre, seatAt(s, v, f - 1, r).centre));
        const float dv = 0.5f * (dist(t.centre, seatAt(s, v, f, r + 1).centre) +
                                 dist(t.centre, seatAt(s, v, f, r - 1).centre));
        CAPTURE(f, r, t.stepU, t.stepV, du, dv);
        CHECK_THAT(t.stepU, WithinAbs(du, 1e-4f));
        CHECK_THAT(t.stepV, WithinAbs(dv, 1e-4f));
      }
    }
  }
}

TEST_CASE("the surface closes the seams the variant declares", "[render]") {
  // The geometry check: a cell on one side of a seam is one step from the cell the
  // identification says is next to it - including the two that reverse a coordinate,
  // where getting the flip wrong lands the neighbour half a board away.
  struct Case {
    std::string name;
    bool fileFlipsRank{false};  ///< crossing the file seam reverses the rank (mobius)
    bool rankGlued{false};      ///< the ranks are glued too (torus, klein)
    bool rankFlipsFile{false};  ///< crossing the rank seam reverses the file (klein)
  };
  const std::vector<Case> cases{{"cylinder", false, false, false},
                                {"torus", false, true, false},
                                {"mobius", true, false, false},
                                {"klein", false, true, true}};
  for (const Case& c : cases) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(c.name));
    const PlaySurface s = PlaySurface::build(v);
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    CAPTURE(c.name);
    // The yardstick is the board's own widest step, not a fixed distance: a Klein
    // bottle's files are unevenly spaced whatever is done to them, and the claim is that
    // the seam is an ordinary step among them rather than a jump across the shape.
    float widestU = 0.0f;
    float widestV = 0.0f;
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        if (f + 1 < nx)
          widestU = std::max(
              widestU, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f + 1, r).centre));
        if (r + 1 < nz)
          widestV = std::max(
              widestV, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f, r + 1).centre));
      }
    }
    for (int r = 0; r < nz; ++r) {
      const SurfaceSeat& a = seatAt(s, v, 0, r);
      const SurfaceSeat& b = seatAt(s, v, nx - 1, c.fileFlipsRank ? nz - 1 - r : r);
      CAPTURE(r, widestU);
      CHECK(dist(a.centre, b.centre) < 1.1f * widestU);
    }
    if (!c.rankGlued) continue;
    for (int f = 0; f < nx; ++f) {
      const SurfaceSeat& a = seatAt(s, v, f, 0);
      const SurfaceSeat& b = seatAt(s, v, c.rankFlipsFile ? nx - 1 - f : f, nz - 1);
      CAPTURE(f, widestV);
      CHECK(dist(a.centre, b.centre) < 1.1f * widestV);
    }
  }
}

TEST_CASE("the shape stands up in the board's own world", "[render]") {
  // The overtures are modelled Y-up and the board's world is Z-up. Handing one straight
  // to the other stands the torus on its rim like a wheel and lays the cylinder on its
  // side, under a camera that orbits about Z. So a donut lies on the table: its hole
  // points up, and it is wider than it is tall.
  const VariantSpec& torus = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface s = PlaySurface::build(torus);
  const view::Bounds b = s.bounds();
  CHECK((b.maxX - b.minX) > 2.0f * (b.maxZ - b.minZ));
  CHECK((b.maxY - b.minY) > 2.0f * (b.maxZ - b.minZ));
  // And the hole is a hole: some cell faces straight up and some straight down.
  float up = -1.0f;
  float down = 1.0f;
  for (const SurfaceSeat& t : s.seats()) {
    up = std::max(up, t.normal.z);
    down = std::min(down, t.normal.z);
  }
  CHECK(up > 0.8f);
  CHECK(down < -0.8f);
}

TEST_CASE("the board slides round its own surface", "[render]") {
  // What the middle button does. Sliding one cell along the files puts a1 exactly where
  // b1 was - exactly, because the slide is the same sampling one cell further along, not
  // an interpolation between two placements. Nothing about the board changes: the same
  // cells are there, each still next to the same neighbours.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const int nx = static_cast<int>(v.dims.extent(0));
    SurfacePose one;
    one.slideU = 1.0f;
    const PlaySurface rest = PlaySurface::build(v);
    const PlaySurface slid = PlaySurface::build(v, one);
    CAPTURE(name);
    REQUIRE(slid.seats().size() == rest.seats().size());
    for (int f = 0; f + 1 < nx; ++f) {
      for (int r = 0; r < static_cast<int>(v.dims.extent(1)); ++r) {
        CHECK_THAT(dist(seatAt(slid, v, f, r).centre, seatAt(rest, v, f + 1, r).centre),
                   WithinAbs(0.0f, 1e-4f));
      }
    }
    // A full period - two laps, `2 * nx` cells - brings the board home exactly. The old
    // wrap was `fmod(s, 2.0f)` in cells, so the board snapped home after *two* squares;
    // this is the assertion that catches it (M17.9).
    SurfacePose period;
    period.slideU = static_cast<float>(2 * nx);
    const PlaySurface home = PlaySurface::build(v, period);
    for (std::size_t i = 0; i < rest.seats().size(); ++i) {
      CHECK(home.seats()[i].cell == rest.seats()[i].cell);
      CHECK_THAT(dist(home.seats()[i].centre, rest.seats()[i].centre),
                 WithinAbs(0.0f, 1e-3f));
    }
    // Two cells is not a lap: the board has moved, it has not come back.
    SurfacePose two;
    two.slideU = 2.0f;
    const PlaySurface moved = PlaySurface::build(v, two);
    bool differs = false;
    for (std::size_t i = 0; i < rest.seats().size(); ++i) {
      if (dist(moved.seats()[i].centre, rest.seats()[i].centre) > 0.1f) differs = true;
    }
    CHECK(differs);
    // Continuity: a slide of a tenth of a cell moves every seat by well under a cell, so
    // the board travels rather than teleporting.
    const PlaySurface tiny = PlaySurface::build(v, [] {
      SurfacePose p;
      p.slideU = 0.1f;
      return p;
    }());
    for (std::size_t i = 0; i < rest.seats().size(); ++i) {
      CHECK(dist(tiny.seats()[i].centre, rest.seats()[i].centre) < 0.5f);
    }
  }
}

TEST_CASE("sliding the ranks is offered only where the ranks are glued", "[render]") {
  // A cylinder's rank edges are free, so sliding along them would translate the whole
  // tube through space and change nothing about the board - a control that did that
  // would be a lie, so the surface ignores it.
  CHECK(PlaySurface::slidesAlongRanks(test::loadVariant("torus")));
  CHECK(PlaySurface::slidesAlongRanks(test::loadVariant("klein")));
  CHECK_FALSE(PlaySurface::slidesAlongRanks(test::loadVariant("cylinder")));
  CHECK_FALSE(PlaySurface::slidesAlongRanks(test::loadVariant("mobius")));

  const VariantSpec& torus = *new VariantSpec(test::loadVariant("torus"));
  SurfacePose one;
  one.slideV = 1.0f;
  const PlaySurface rest = PlaySurface::build(torus);
  const PlaySurface slid = PlaySurface::build(torus, one);
  for (int f = 0; f < 8; ++f) {
    for (int r = 0; r + 1 < 8; ++r) {
      CHECK_THAT(
          dist(seatAt(slid, torus, f, r).centre, seatAt(rest, torus, f, r + 1).centre),
          WithinAbs(0.0f, 1e-4f));
    }
  }
}

TEST_CASE("invert swaps the side a piece stands on, without moving the board",
          "[render]") {
  // M17.7, revised: INVERT does not mirror the geometry - the eversion that ships in the
  // library's shapes moves every cell about the origin, which is not what a board wants.
  // It swaps the outward side: the squares stay exactly where they were, and every piece
  // stands on the other face. At rest the surface is the library screen's, bit for bit,
  // which every existing capture depends on.
  const VariantSpec& torus = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface rest = PlaySurface::build(torus, SurfacePose{});
  for (const SurfaceSeat& t : rest.seats()) {
    const Coord co = torus.dims.toCoord(t.cell);
    const float u = (static_cast<float>(co.c[0]) + 0.5f) / 8.0f;
    const float vv = (static_cast<float>(co.c[1]) + 0.5f) / 8.0f;
    const OvVec3 want = derivedSurfaceAt(app::overtureSignature(torus).surface, u, vv);
    // The same point, in the board's world: the shape is the overture's, turned upright.
    CHECK_THAT(dist(t.centre, view::Vec3{want.x, -want.z, want.y}),
               WithinAbs(0.0f, 1e-5f));
  }
  SurfacePose full;
  full.evert = 1.0f;
  const PlaySurface turned = PlaySurface::build(torus, full);
  REQUIRE(turned.seats().size() == rest.seats().size());
  for (std::size_t i = 0; i < rest.seats().size(); ++i) {
    // The geometry is identical...
    CHECK_THAT(dist(turned.seats()[i].centre, rest.seats()[i].centre),
               WithinAbs(0.0f, 1e-6f));
    // ...and the normal is exactly reversed, so the piece stands on the other side.
    CHECK_THAT(view::dot(turned.seats()[i].normal, rest.seats()[i].normal),
               WithinAbs(-1.0f, 1e-4f));
    CHECK(turned.seats()[i].stepU > 0.05f);
    CHECK(turned.seats()[i].stepV > 0.05f);
  }
}

TEST_CASE("the invert is pure in its parameter and never moves the board", "[render]") {
  // A pose is a function of one number, so a capture reproduces and a toggle is
  // reversible. The side swap is discrete (past the halfway point), so this pins
  // determinism and that the squares do not move - which is the whole point of the
  // revised invert.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    for (int i = 0; i <= 10; ++i) {
      CAPTURE(name, i);
      SurfacePose p;
      p.evert = static_cast<float>(i) / 10.0f;
      const PlaySurface a = PlaySurface::build(v, p);
      const PlaySurface b = PlaySurface::build(v, p);
      REQUIRE(a.seats().size() == rest.seats().size());
      for (std::size_t c = 0; c < a.seats().size(); ++c) {
        // Pure...
        CHECK_THAT(dist(a.seats()[c].centre, b.seats()[c].centre),
                   WithinAbs(0.0f, 1e-6f));
        CHECK_THAT(view::dot(a.seats()[c].normal, b.seats()[c].normal),
                   WithinAbs(1.0f, 1e-6f));
        // ...and the squares never move, at any point of the turn.
        CHECK_THAT(dist(a.seats()[c].centre, rest.seats()[c].centre),
                   WithinAbs(0.0f, 1e-6f));
      }
    }
  }
}

TEST_CASE("picking the surface follows what was drawn", "[render]") {
  // ADR-0011's invariant: the ray tests the surface the tiles were built from, so a
  // click can never select a cell other than the one under the cursor. Every cell whose
  // centre is on screen and in front picks itself back.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const float w = 900.0f;
    const float h = 700.0f;
    const view::OrbitCamera cam = view::OrbitCamera::frame(s.bounds(), w / h);
    int hits = 0;
    int agreed = 0;
    CAPTURE(name);
    for (const SurfaceSeat& t : s.seats()) {
      const view::OrbitCamera::ScreenPoint sp = cam.project(t.centre, w / h, w, h);
      if (!sp.visible) continue;
      const CellId got = s.pick(cam, w, h, sp.x, sp.y);
      if (got == kInvalidCell) continue;
      ++hits;
      // Either this cell, or one in front of it - a cell round the back is hidden, and
      // picking the near one is the right answer, not a miss.
      if (got == t.cell) {
        ++agreed;
        continue;
      }
      const auto it = std::find_if(s.seats().begin(), s.seats().end(),
                                   [&](const SurfaceSeat& o) { return o.cell == got; });
      REQUIRE(it != s.seats().end());
      // Whatever came back is under that pixel too - it is the sheet of the surface in
      // front of this one.
      const view::OrbitCamera::ScreenPoint gp = cam.project(it->centre, w / h, w, h);
      CAPTURE(t.cell, got, sp.x, sp.y, gp.x, gp.y);
      CHECK(std::hypot(gp.x - sp.x, gp.y - sp.y) < 90.0f);
    }
    CHECK(hits > 40);
    // A Klein bottle is an immersion: it passes through itself, so a good number of its
    // cells are genuinely behind another sheet of the same board and picking the sheet in
    // front is the right answer, checked above.
    CHECK(agreed > hits / 3);
    // A pixel in the corner, off the shape entirely, is nothing at all.
    CHECK(s.pick(cam, w, h, 3.0f, 3.0f) == kInvalidCell);
  }
}

TEST_CASE("only a glued two-dimensional board has a play surface", "[render]") {
  CHECK(PlaySurface::build(test::loadVariant("standard")).empty());
  CHECK(PlaySurface::build(test::loadVariant("mirrorbox")).empty());
  CHECK(PlaySurface::build(test::loadVariant("cube5")).empty());
  CHECK_FALSE(PlaySurface::build(test::loadVariant("torus")).empty());
}

TEST_CASE("the shape is framed on its own centre", "[render]") {
  // M17.11. `PlaySurface::bounds()` already pads for the pieces, so the camera's headroom
  // must be 0 when framing on it - otherwise the look-at is lifted off the shape's centre
  // and the board drops down the window. The projected bounds centre is the window
  // centre.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const view::Bounds b = s.bounds();
    const view::Vec3 centre{(b.minX + b.maxX) * 0.5f, (b.minY + b.maxY) * 0.5f,
                            (b.minZ + b.maxZ) * 0.5f};
    for (const float aspect : {1.6f, 0.8f}) {
      CAPTURE(name, aspect);
      const view::OrbitCamera cam = view::OrbitCamera::frame(b, aspect, 0.0f);
      const float w = 1600.0f;
      const float h = w / aspect;
      const view::OrbitCamera::ScreenPoint sp = cam.project(centre, aspect, w, h);
      REQUIRE(sp.visible);
      CHECK(std::abs(sp.x - w * 0.5f) < 0.02f * w);
      CHECK(std::abs(sp.y - h * 0.5f) < 0.02f * h);
    }
  }
}

TEST_CASE("a three- and four-dimensional variant plays on its own shape", "[render]") {
  // M17.12, first two shapes: `torus3d`'s nested shells and `hyper4`'s tesseract. The
  // shape is authored, not derived, so it is keyed by name; `cube5` has none and stays on
  // the ordinary lattice. Every cell gets a tile and a seat, sized from its neighbours.
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    CHECK(hasPlaySurface(v));
    const PlaySurface s = PlaySurface::build(v);
    REQUIRE(s.seats().size() == v.dims.cellCount());
    REQUIRE(s.patches().size() == v.dims.cellCount());
    for (const SurfaceSeat& seat : s.seats()) {
      CHECK(seat.stepU > 0.01f);
      CHECK(seat.stepV > 0.01f);
      CHECK_THAT(view::length(seat.normal), WithinAbs(1.0f, 1e-4f));
    }
    // A real extent, so the camera can frame it.
    const view::Bounds b = s.bounds();
    CHECK(b.maxX - b.minX > 1.0f);
    CHECK(b.maxY - b.minY > 1.0f);

    if (std::string(name) == "torus3d") {
      // A periodic axis has no special cell (the variant's own comment: "no cell is
      // special"), so within one shell the *file* step - the cross-section circle - must
      // be the same everywhere. The clamp bug made file 0/3 differ from file 1/2 by 41%;
      // this fails on that and passes after M17.13. The rank step is not uniform (it
      // varies with the cross-section offset), and the shells differ from one another in
      // radius, so only `stepU` is compared, within a level.
      std::map<int, float> ref;
      for (const SurfaceSeat& seat : s.seats()) {
        const int level = v.dims.toCoord(seat.cell).c[2];
        const auto it = ref.find(level);
        if (it == ref.end()) {
          ref.emplace(level, seat.stepU);
          continue;
        }
        CHECK_THAT(seat.stepU, Catch::Matchers::WithinRel(it->second, 0.03f));
      }
    }
    if (std::string(name) == "hyper4") {
      // All four axes are bounded, so the boundary cell takes a one-sided step scaled to
      // one cell - not the clamped two-sided one halved to half a cell (M17.13). The
      // boundary must not be the systematic half-size the bug produced.
      const auto stepUAt = [&](int f, int r, int L, int A) {
        const CellId want = v.dims.toCell(Coord::of({f, r, L, A}));
        for (const SurfaceSeat& seat : s.seats()) {
          if (seat.cell == want) return seat.stepU;
        }
        return 0.0f;
      };
      CHECK(stepUAt(0, 1, 1, 1) > 0.6f * stepUAt(1, 1, 1, 1));
    }
  }
  CHECK_FALSE(hasPlaySurface(test::loadVariant("cube5")));
  CHECK(PlaySurface::build(test::loadVariant("cube5")).empty());
}

TEST_CASE("picking a three- and four-dimensional shape finds the tile", "[render]") {
  for (const char* name : {"torus3d", "hyper4"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface s = PlaySurface::build(v);
    const float w = 1000.0f;
    const float h = 760.0f;
    const view::OrbitCamera cam = view::OrbitCamera::frame(s.bounds(), w / h, 0.0f);
    int hits = 0;
    int agreed = 0;
    for (const SurfaceSeat& t : s.seats()) {
      const view::OrbitCamera::ScreenPoint sp = cam.project(t.centre, w / h, w, h);
      if (!sp.visible) continue;
      const CellId got = s.pick(cam, w, h, sp.x, sp.y);
      if (got == kInvalidCell) continue;
      ++hits;
      if (got == t.cell) {
        ++agreed;
        continue;
      }
      // Otherwise it is the sheet of the shape in front of this one, which is the right
      // answer - and it is still under the pixel.
      const auto it = std::find_if(s.seats().begin(), s.seats().end(),
                                   [&](const SurfaceSeat& o) { return o.cell == got; });
      REQUIRE(it != s.seats().end());
      const view::OrbitCamera::ScreenPoint gp = cam.project(it->centre, w / h, w, h);
      CHECK(std::hypot(gp.x - sp.x, gp.y - sp.y) < 120.0f);
    }
    CHECK(hits > 30);
    // A self-intersecting shape genuinely hides cells behind nearer sheets - the
    // tesseract more than the nested shells - so a pick to the *front* cell is the
    // correct answer, not a miss. The property that matters is the one checked above:
    // whatever comes back is under the pixel.
    CHECK(agreed > hits / 5);
    // A pixel off the shape is nothing.
    CHECK(s.pick(cam, w, h, 2.0f, 2.0f) == kInvalidCell);
  }
}

TEST_CASE("the camera reframes on the shape when the variant changes under it",
          "[render]") {
  // M17.14: `geometryView` is sticky across `Shell::startGame` (never reset), so a
  // one-shot "did the toggle flip" check misses a variant switch that happens while it is
  // already on - the camera is left on the previous variant's shape, or the new variant's
  // flat board. Framing unconditionally on the load fixes it.
  auto open = [](const char* name) {
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s.has_value());
    return std::move(*s);
  };
  BoardOptions opts;
  opts.surface = true;

  std::unique_ptr<app::Session> session = open("torus");
  frameGeometryCamera(*session, opts, SurfacePose{});
  session = open("torus3d");  // switched variant; opts.surface is still true throughout
  frameGeometryCamera(*session, opts, SurfacePose{});

  const view::Bounds want = PlaySurface::build(test::loadVariant("torus3d")).bounds();
  const view::Vec3 target = session->camera().target;
  CHECK_THAT(target.x, WithinAbs(want.centerX(), 0.05f));
  CHECK_THAT(target.y, WithinAbs(want.centerY(), 0.05f));
  CHECK_THAT(target.z, WithinAbs(want.centerZ(), 0.05f));
}

TEST_CASE("a move on the shape is sampled along the surface", "[render]") {
  // M17.15: the travelling piece is drawn part-way between its seats, not teleported to
  // its landing square. Pure geometry, no GPU.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface surf = PlaySurface::build(v);
  const auto seatOf = [&](CellId c) -> const SurfaceSeat* {
    for (const SurfaceSeat& s : surf.seats()) {
      if (s.cell == c) return &s;
    }
    return nullptr;
  };
  const auto cell = [&](int f, int r) { return v.dims.toCell(Coord::of({f, r})); };

  // A leap arcs from the start seat to the end, clear of the straight chord.
  {
    view::MovePath path;
    path.from = cell(0, 0);
    path.to = cell(0, 3);
    path.leap = true;
    const SurfaceSeat* a = seatOf(path.from);
    const SurfaceSeat* b = seatOf(path.to);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 0.0f).position, a->centre),
               WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 1.0f).position, b->centre),
               WithinAbs(0.0f, 1e-4f));
    const SurfaceMoveSample mid = surfaceMoveSample(path, surf, 0.5f);
    const view::Vec3 chord = (a->centre + b->centre) * 0.5f;
    CHECK(dist(mid.position, chord) > 0.05f);  // it arcs, it does not cut through
    CHECK_THAT(view::length(mid.normal), WithinAbs(1.0f, 1e-3f));
  }

  // A glide walks the seats the route passes through, in order, hugging the surface.
  {
    view::MovePath path;
    path.from = cell(0, 0);
    path.to = cell(2, 0);
    path.steps = {{cell(0, 0), cell(1, 0), view::StepKind::Interior, {}, 0, Side::Max},
                  {cell(1, 0), cell(2, 0), view::StepKind::Interior, {}, 0, Side::Max}};
    const SurfaceSeat* a = seatOf(path.from);
    const SurfaceSeat* mid = seatOf(cell(1, 0));
    const SurfaceSeat* z = seatOf(path.to);
    REQUIRE(a != nullptr);
    REQUIRE(mid != nullptr);
    REQUIRE(z != nullptr);
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 0.0f).position, a->centre),
               WithinAbs(0.0f, 1e-4f));
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 1.0f).position, z->centre),
               WithinAbs(0.0f, 1e-4f));
    // It passes over the middle square (laterally, at some t) and hovers there
    // (vertically): the surface boundary waypoints make the arc-length midpoint a touch
    // off the exact centre, so scan rather than pin t = 0.5.
    float bestLateral = 1e9f;
    float bestLift = 0.0f;
    for (float tt = 0.0f; tt <= 1.0001f; tt += 0.002f) {
      const SurfaceMoveSample s = surfaceMoveSample(path, surf, tt);
      const view::Vec3 off = s.position - mid->centre;
      bestLateral =
          std::min(bestLateral, view::length(off - s.normal * view::dot(off, s.normal)));
      bestLift = std::max(bestLift, view::dot(off, s.normal));
    }
    CHECK(bestLateral < 0.01f);
    CHECK(bestLift > 0.05f);
    for (float t = 0.0f; t <= 1.0001f; t += 0.1f) {
      CHECK(view::length(surfaceMoveSample(path, surf, t).normal) > 0.9f);
    }
  }

  // A glide reaches the boundary between two squares and hovers over it, rather than
  // cutting a straight chord; the waypoint is the shared edge (orthogonal) or shared
  // corner (diagonal), lifted off the board so the base clears (M17.19).
  {
    for (const std::pair<int, int>& to : {std::pair{1, 0}, std::pair{1, 1}}) {
      view::MovePath path;
      path.from = cell(0, 0);
      path.to = cell(to.first, to.second);
      path.steps = {{cell(0, 0),
                     cell(to.first, to.second),
                     view::StepKind::Interior,
                     {},
                     0,
                     Side::Max}};
      const SurfaceSeat* a = seatOf(path.from);
      const SurfaceSeat* b = seatOf(path.to);
      REQUIRE(a != nullptr);
      REQUIRE(b != nullptr);
      const SurfaceMoveSample mid = surfaceMoveSample(path, surf, 0.5f);
      const view::Vec3 chord = (a->centre + b->centre) * 0.5f;
      CAPTURE(to.first, to.second);
      CHECK(view::dot(mid.position - chord, mid.normal) > 0.05f);  // lifted off the chord
      CHECK_THAT(dist(surfaceMoveSample(path, surf, 0.0f).position, a->centre),
                 WithinAbs(0.0f, 1e-4f));
      CHECK_THAT(dist(surfaceMoveSample(path, surf, 1.0f).position, b->centre),
                 WithinAbs(0.0f, 1e-4f));
    }
  }

  // `pointAt` samples the surface: a cell centre comes back exactly, and a boundary is a
  // point *on* the surface, not the chord midpoint of the two seats - the two differ on a
  // curving board, which is exactly why the chord midpoint clipped (M17.19).
  {
    view::Vec3 centre{};
    REQUIRE(surf.pointAt(0.5f, 0.5f, centre));
    const SurfaceSeat& s00 = *seatOf(cell(0, 0));
    CHECK_THAT(dist(centre, s00.centre), WithinAbs(0.0f, 1e-4f));
    float worst = 0.0f;
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    for (int f = 0; f + 1 < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        const SurfaceSeat& p = *seatOf(cell(f, r));
        const SurfaceSeat& q = *seatOf(cell(f + 1, r));
        view::Vec3 edge{};
        REQUIRE(surf.nearestBoundary(p, q, edge));
        worst = std::max(worst, dist(edge, (p.centre + q.centre) * 0.5f));
      }
    }
    CHECK(worst > 0.01f);  // the surface boundary is genuinely not the chord midpoint
  }

  // A glide across the glued file edge is not cut: on the surface the seam is one
  // continuous place, so consecutive samples stay within a cell of each other.
  {
    view::MovePath path;
    path.from = cell(0, 0);
    path.to = cell(7, 0);
    path.steps = {{cell(0, 0), cell(7, 0), view::StepKind::Portal, {}, 0, Side::Min}};
    view::Vec3 prev = surfaceMoveSample(path, surf, 0.0f).position;
    for (float t = 0.05f; t <= 1.0001f; t += 0.05f) {
      const view::Vec3 cur = surfaceMoveSample(path, surf, t).position;
      CHECK(dist(cur, prev) < 1.0f);
      prev = cur;
    }
  }
}

TEST_CASE("the move camera can follow on the shape", "[render]") {
  // M17.16: `cameraOver` is the one blend, asked against the shape's placements. With
  // following off it is the settled framing (a strict no-op); with it on the target
  // tracks the route the engine already computed.
  auto session = app::Session::create(test::loadVariant("torus"));
  REQUIRE(session.has_value());
  app::Session& s = **session;
  const PlaySurface surf = PlaySurface::build(s.variant());
  std::vector<view::Placement> places;
  places.reserve(surf.seats().size());
  for (const SurfaceSeat& seat : surf.seats()) {
    places.push_back({seat.cell, seat.centre.x, seat.centre.y, seat.centre.z, 0});
  }

  // Off is bit-identical to the session's own camera.
  const view::OrbitCamera off = s.cameraOver(places, surf.bounds(), view::ViewConfig{});
  const view::OrbitCamera plain = s.camera();
  CHECK_THAT(dist(off.target, plain.target), WithinAbs(0.0f, 1e-5f));

  // On, a shot's target moves with the route.
  s.setCameraMode("route");
  s.setFollowStrength(1.0f);
  REQUIRE_FALSE(s.game().legalMoves().empty());
  const Move m = s.game().legalMoves().front();
  const auto click = [&](CellId c) {
    app::Action a;
    a.kind = app::ActionKind::ClickCell;
    a.cell = c;
    REQUIRE(s.apply(a).has_value());
  };
  click(m.from);
  click(m.to);
  REQUIRE(s.shotInFlight());
  s.setMoveProgress(0.0f);
  const view::Vec3 start = s.cameraOver(places, surf.bounds(), view::ViewConfig{}).target;
  s.setMoveProgress(0.5f);  // the shot envelope is zero at the ends, so sample mid-move
  const view::Vec3 mid = s.cameraOver(places, surf.bounds(), view::ViewConfig{}).target;
  CHECK(dist(start, mid) > 0.05f);
}

TEST_CASE("align turns an inner-ring cell toward the camera", "[render]") {
  // M17.17: a search over slide offsets that maximises the followed cell's `facing` -
  // whether its outward normal points toward the camera. An open tube has no inner/outer
  // side, so it returns 0.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface rest = PlaySurface::build(v);
  const auto seatFor = [&](const PlaySurface& s, CellId c) -> const SurfaceSeat* {
    for (const SurfaceSeat& seat : s.seats()) {
      if (seat.cell == c) return &seat;
    }
    return nullptr;
  };
  // The camera's own eye direction, shared with the anti-clip search so the two cannot
  // disagree (M17.20).
  const auto toEyeFor = [](const SurfaceSeat& seat, const view::Vec3& travel) {
    return chaseEyeDirection(seat.normal, travel, kDefaultFollowLift);
  };

  // The cell facing most away from a nominal camera at rest: the case to fix.
  const view::Vec3 restTravel{1.0f, 0.0f, 0.0f};
  CellId inner = kInvalidCell;
  float worst = 1e9f;
  for (const SurfaceSeat& seat : rest.seats()) {
    const float facing = view::dot(seat.normal, toEyeFor(seat, restTravel));
    if (facing < worst) {
      worst = facing;
      inner = seat.cell;
    }
  }
  REQUIRE(inner != kInvalidCell);

  const SurfaceSeat* base = seatFor(rest, inner);
  REQUIRE(base != nullptr);
  const view::Vec3 travel = byQuat(base->quat, view::Vec3{1, 0, 0});  // a real tangent
  const SlideOffset offset = alignSlideU(v, inner, travel, 6.0f);
  SurfacePose pose;
  pose.slideU = offset.u;
  pose.slideV = offset.v;
  const PlaySurface turned = PlaySurface::build(v, pose);
  const SurfaceSeat* fixed = seatFor(turned, inner);
  REQUIRE(fixed != nullptr);
  const view::Vec3 toEye = toEyeFor(*fixed, travel);
  const float eyeDist = clearEyeDistance(turned, fixed->centre, toEye, 6.0f);
  const view::Vec3 eye = fixed->centre + toEye * eyeDist;
  const float facing = view::dot(fixed->normal, toEye);
  CHECK(facing > worst);  // the search improves the facing it targets
  CHECK(facing > 0.0f);   // and lands the piece on the camera's side
  CHECK_FALSE(turned.blocked(eye, fixed->centre, 0.02f));  // nothing between the two

  // An open tube/ribbon has nothing to align.
  const SlideOffset none = alignSlideU(test::loadVariant("cylinder"), 0, travel, 6.0f);
  CHECK(none.u == 0.0f);
  CHECK(none.v == 0.0f);
  CHECK(alignSlideU(test::loadVariant("mobius"), 0, travel, 6.0f).u == 0.0f);
}

TEST_CASE("Klein's normal field is continuous except at its seam", "[render]") {
  // M17.18. A non-orientable shape has no globally consistent outward normal, so one flip
  // must exist; the defect was that it landed *twice*, mid-board, at the cross-section's
  // numerically unstable pinch. The continuity walk moves it to the one place a player
  // expects it - the glued file edge - and nowhere else.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  constexpr int kSide = kSurfaceSubdiv + 1;
  const auto patchOf = [&](const PlaySurface& s, int f, int r) -> const SurfacePatch& {
    const CellId want = v.dims.toCell(Coord::of({f, r}));
    for (const SurfacePatch& p : s.patches()) {
      if (p.cell == want) return p;
    }
    throw std::runtime_error("no patch");
  };
  const auto seatOf = [&](const PlaySurface& s, int f, int r) -> const SurfaceSeat& {
    const CellId want = v.dims.toCell(Coord::of({f, r}));
    for (const SurfaceSeat& t : s.seats()) {
      if (t.cell == want) return t;
    }
    throw std::runtime_error("no seat");
  };

  for (const float slide : {0.0f, 1.0f, 2.0f, 3.5f}) {
    CAPTURE(slide);
    SurfacePose pose;
    pose.slideU = slide;
    const PlaySurface s = PlaySurface::build(v, pose);
    for (int r = 0; r < nz; ++r) {
      for (int f = 0; f + 1 < nx; ++f) {
        // Within one square, and across the gap into the next file: never opposed.
        const SurfacePatch& p = patchOf(s, f, r);
        for (int j = 0; j < kSide; ++j) {
          for (int i = 0; i + 1 < kSide; ++i) {
            CHECK(view::dot(p.normal[static_cast<std::size_t>(i * kSide + j)],
                            p.normal[static_cast<std::size_t>((i + 1) * kSide + j)]) >
                  0.0f);
          }
        }
        const SurfacePatch& q = patchOf(s, f + 1, r);
        for (int j = 0; j < kSide; ++j) {
          CHECK(view::dot(p.normal[static_cast<std::size_t>(kSurfaceSubdiv * kSide + j)],
                          q.normal[static_cast<std::size_t>(j)]) > 0.0f);
        }
        // The piece's own frame is continuous too.
        const view::Vec3 ea = byQuat(seatOf(s, f, r).quat, view::Vec3{1, 0, 0});
        const view::Vec3 eb = byQuat(seatOf(s, f + 1, r).quat, view::Vec3{1, 0, 0});
        CHECK(view::dot(ea, eb) > 0.0f);
      }
      // The file axis is the orientation-preserving one here (klein glues its *rank* with
      // a file flip), so the field closes across the file wrap too. The single
      // unavoidable flip lives at the rank seam, which a per-rank walk deliberately does
      // not close.
      const SurfacePatch& last = patchOf(s, nx - 1, r);
      const SurfacePatch& first = patchOf(s, 0, r);
      for (int j = 0; j < kSide; ++j) {
        CHECK(view::dot(last.normal[static_cast<std::size_t>(kSurfaceSubdiv * kSide + j)],
                        first.normal[static_cast<std::size_t>(j)]) > 0.0f);
      }
    }
  }
}

TEST_CASE("the chase camera centres a piece, and stands it up or lets it tilt",
          "[render]") {
  // M17.16 (revised): following a move is one continuous chase, the piece centred.
  // Upright mode also rolls the frame so the piece's own up (surface normal) points up
  // the screen; the tilt mode does not, so the piece rides the shape (the other camera
  // mode).
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface surf = PlaySurface::build(v);
  const float w = 1000.0f;
  const float h = 700.0f;
  const float aspect = w / h;
  for (std::size_t i = 0; i < surf.seats().size(); i += 7) {
    const SurfaceSeat& seat = surf.seats()[i];
    SurfaceMoveSample piece;
    piece.position = seat.centre;
    piece.normal = seat.normal;
    const view::Vec3 travel = byQuat(seat.quat, view::Vec3{1, 0, 0});
    const view::OrbitCamera cam = surfaceChaseCamera(piece, travel, 4.0f, true);
    const view::OrbitCamera::ScreenPoint sp = cam.project(piece.position, aspect, w, h);
    REQUIRE(sp.visible);
    CHECK(std::abs(sp.x - w * 0.5f) < 1.0f);
    CHECK(std::abs(sp.y - h * 0.5f) < 1.0f);
    // The normal points up the screen, and not sideways.
    const view::OrbitCamera::ScreenPoint up =
        cam.project(piece.position + piece.normal, aspect, w, h);
    CHECK(up.y < sp.y);
    CHECK(std::abs(up.x - sp.x) < 8.0f);

    // The tilt mode centres the piece too, but does not roll: world up stays screen up,
    // so the piece is not forced vertical.
    const view::OrbitCamera tilt = surfaceChaseCamera(piece, travel, 4.0f, false);
    CHECK_THAT(tilt.roll, WithinAbs(0.0f, 1e-6f));
    const view::OrbitCamera::ScreenPoint tsp = tilt.project(piece.position, aspect, w, h);
    CHECK(std::abs(tsp.x - w * 0.5f) < 1.0f);
    CHECK(std::abs(tsp.y - h * 0.5f) < 1.0f);
  }
}

TEST_CASE("the chase camera's up really is the piece's up", "[render]") {
  // The stronger form of the claim above: not merely that the normal points *roughly*
  // up the screen, but that the view's up axis is the piece's normal projected
  // perpendicular to the view direction - the vectors are aligned. The loose check above
  // did not catch the inverted roll; this checks every seat and both surface tangents.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface surf = PlaySurface::build(v);
    const float w = 1000.0f;
    const float h = 700.0f;
    const float aspect = w / h;
    for (std::size_t i = 0; i < surf.seats().size(); ++i) {
      const SurfaceSeat& seat = surf.seats()[i];
      SurfaceMoveSample piece;
      piece.position = seat.centre;
      piece.normal = seat.normal;
      piece.quat = seat.quat;
      CAPTURE(name, i, seat.centre.x, seat.centre.y, seat.centre.z);
      // The frame the renderer gives the piece: local +Z is the surface normal.
      CHECK_THAT(view::dot(byQuat(seat.quat, view::Vec3{0, 0, 1}), seat.normal),
                 WithinAbs(1.0f, 1e-3f));
      for (const view::Vec3& local : {view::Vec3{1, 0, 0}, view::Vec3{0, 1, 0}}) {
        const view::Vec3 travel = byQuat(seat.quat, local);
        const view::OrbitCamera cam = surfaceChaseCamera(piece, travel, 4.0f, true);
        const view::OrbitCamera::ScreenPoint sp =
            cam.project(piece.position, aspect, w, h);
        REQUIRE(sp.visible);
        const view::OrbitCamera::ScreenPoint up =
            cam.project(piece.position + piece.normal, aspect, w, h);
        const float upLen = sp.y - up.y;  // positive: the normal points up the screen
        CHECK(upLen > 0.0f);
        // Straight up and not sideways: the horizontal offset is a rounding error
        // against the vertical run, never a real lean.
        CHECK(std::abs(up.x - sp.x) < 0.02f * std::abs(upLen) + 0.5f);
      }
    }
  }
}

TEST_CASE("the chase frame's axes are the piece's own: right a x b, up c x n",
          "[render]") {
  // The exact frame the camera is built from, not "the piece roughly upright": `a` is the
  // direction of motion, `b` the piece's upright, `n = a x b`, and `c` the back-and-up
  // vector (`-a` rotated up by the elevation: the piece-to-eye direction). The camera's
  // right must be `n` and its up `c x n`. The looser test above would not catch a wrong
  // horizontal, which is why this reads the basis out of the matrix the renderer uses.
  for (const std::string& name : kShapes) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface surf = PlaySurface::build(v);
    const float aspect = 1000.0f / 700.0f;
    for (std::size_t i = 0; i < surf.seats().size(); i += 5) {
      const SurfaceSeat& seat = surf.seats()[i];
      SurfaceMoveSample piece;
      piece.position = seat.centre;
      piece.normal = seat.normal;
      piece.quat = seat.quat;
      const view::Vec3 a0 = byQuat(seat.quat, view::Vec3{1, 0, 0});
      const view::OrbitCamera cam = surfaceChaseCamera(piece, a0, 4.0f, true);
      const view::Mat4 vp = cam.viewProj(aspect);
      // `right` is lookFrom's first row; the world direction that points *up* the screen
      // is the negation of the second row, because the projection flips Y for Vulkan.
      const view::Vec3 right = view::normalize(view::Vec3{vp[0], vp[4], vp[8]});
      const view::Vec3 up = view::normalize(view::Vec3{-vp[1], -vp[5], -vp[9]});
      // The frame the construction asks for.
      const view::Vec3 a = view::normalize(a0);
      view::Vec3 b = piece.normal - a * view::dot(piece.normal, a);
      b = view::normalize(b);
      const view::Vec3 n = view::normalize(view::cross(a, b));
      const float lift = kDefaultFollowLift;
      const float cosT = 1.0f / std::sqrt(1.0f + lift * lift);
      const float sinT = lift * cosT;
      const view::Vec3 c = view::normalize(a * -cosT + b * sinT);
      const view::Vec3 wantUp = view::normalize(view::cross(c, n));
      CAPTURE(name, i);
      CHECK_THAT(view::dot(right, n), WithinAbs(1.0f, 1e-3f));
      CHECK_THAT(view::dot(up, wantUp), WithinAbs(1.0f, 1e-3f));
    }
  }
}

namespace {

/// The camera's screen-up axis in world space, read from the matrix it actually projects
/// with (`viewProj`), so a basis the renderer would use is what is compared - not a
/// re-derivation from the pose fields.
view::Vec3 screenUp(const view::OrbitCamera& cam, float aspect) {
  const view::Mat4 vp = cam.viewProj(aspect);
  return view::normalize(view::Vec3{vp[1], vp[5], vp[9]});
}

float angleBetween(const view::Vec3& a, const view::Vec3& b) {
  return std::acos(std::clamp(view::dot(a, b), -1.0f, 1.0f));
}

}  // namespace

TEST_CASE("the follow camera is smooth along a move", "[render]") {
  // "Jerky" is the camera swivelling at a cell corner or flipping over a leap's apex.
  // The follow camera is pure in `t`, so smoothness is a property of the function: a
  // fine sweep turns the view by a small angle each step and never by a half-turn.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface surf = PlaySurface::build(v);
  const auto cell = [&](int f, int r) { return v.dims.toCell(Coord::of({f, r})); };
  const float aspect = 1000.0f / 700.0f;

  const auto smoothness = [&](const view::MovePath& path, const char* what) {
    CAPTURE(what);
    const int steps = 200;
    view::Vec3 prevEye{};
    view::Vec3 prevUp{};
    float worstEye = 0.0f;
    float worstUp = 0.0f;
    int worstAt = 0;
    for (int i = 0; i <= steps; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(steps);
      const view::OrbitCamera cam = surfaceFollowCamera(path, surf, t, 4.0f, true);
      const view::Vec3 eyeDir = view::normalize(cam.eye() - cam.target);
      const view::Vec3 up = screenUp(cam, aspect);
      if (i > 0) {
        const float de = angleBetween(eyeDir, prevEye);
        const float du = angleBetween(up, prevUp);
        if (de > worstEye) {
          worstEye = de;
          worstAt = i;
        }
        worstUp = std::max(worstUp, du);
      }
      prevEye = eyeDir;
      prevUp = up;
    }
    CAPTURE(worstAt, static_cast<float>(worstAt) / static_cast<float>(steps));
    // ~0.03 rad per step is an ordinary sweep over this move; a cell-corner swivel or a
    // leap flip is many times that.
    CHECK(worstEye < 0.12f);
    CHECK(worstUp < 0.12f);
  };

  // A rook's glide around the ring, at the tube position that faces most steadily up:
  // the outward side, which is where the anti-clip keeps a followed piece. (A route
  // across the underside swings any third-person camera through a pole; the shipped
  // anti-clip turns the shape so that does not happen, which is tested separately.)
  const int nx = static_cast<int>(v.dims.extent(0));
  int topFile = 0;
  float bestUp = -2.0f;
  for (int f = 0; f < nx; ++f) {
    if (seatAt(surf, v, f, 0).normal.z > bestUp) {
      bestUp = seatAt(surf, v, f, 0).normal.z;
      topFile = f;
    }
  }
  view::MovePath glide;
  glide.from = cell(topFile, 0);
  glide.to = cell(topFile, 6);
  for (int r = 0; r < 6; ++r) {
    glide.steps.push_back({cell(topFile, r),
                           cell(topFile, r + 1),
                           view::StepKind::Interior,
                           {},
                           0,
                           Side::Max});
  }
  smoothness(glide, "glide");

  // A knight's leap, whose arc reverses at its apex.
  view::MovePath leap;
  leap.from = cell(0, 0);
  leap.to = cell(2, 1);
  leap.leap = true;
  smoothness(leap, "leap");
}

TEST_CASE("the turntable align turns the piece to face the camera, unoccluded",
          "[render]") {
  // The turntable's anti-clip: the camera angle is fixed and the *shape* rotates so the
  // followed cell comes round to the near side. For every cell, the chosen offset must
  // leave the piece facing the eye and nothing of the shape between the two.
  for (const char* name : {"torus", "klein"}) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    const view::Bounds rb = rest.bounds();
    const float restSpan =
        std::max({rb.maxX - rb.minX, rb.maxY - rb.minY, rb.maxZ - rb.minZ});
    const view::Vec3 toCamera = view::normalize(view::Vec3{0.35f, -1.0f, 0.55f});
    int checked = 0;
    int unoccluded = 0;
    int facing = 0;
    for (const SurfaceSeat& seat : rest.seats()) {
      const SlideOffset offset =
          alignSlideToFace(v, seat.cell, toCamera, 0.6f * restSpan);
      SurfacePose pose;
      pose.slideU = offset.u;
      pose.slideV = offset.v;
      const PlaySurface turned = PlaySurface::build(v, pose);
      const SurfaceSeat* fixed = [&]() -> const SurfaceSeat* {
        for (const SurfaceSeat& s : turned.seats()) {
          if (s.cell == seat.cell) return &s;
        }
        return nullptr;
      }();
      REQUIRE(fixed != nullptr);
      ++checked;
      const view::Bounds b = turned.bounds();
      const float span = std::max({b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ});
      const view::Vec3 eye = fixed->centre + toCamera * (0.6f * span);
      if (view::dot(fixed->normal, toCamera) > 0.0f) ++facing;
      if (!turned.blocked(eye, fixed->centre, 0.02f)) ++unoccluded;
    }
    CAPTURE(name, checked, facing, unoccluded);
    // Every cell can be brought clear of the shape on a closed ring: the search prefers
    // an unoccluded seat over a better-facing one, so nothing clips.
    CHECK(unoccluded == checked);
    // ...and on the orientable torus every cell also faces the camera. On the Klein
    // bottle only the V offset is searched (the U offset was measured to tear the
    // tiling), so its cells are not all turned to face the eye - but none is hidden.
    const bool orientable = std::string(name) == "torus";
    CHECK(facing >= (orientable ? checked : checked / 2));
  }
}

TEST_CASE("the chase align clears the shape at the camera's own distance", "[render]") {
  // The search has to test occlusion at the distance the camera will actually sit: a
  // close chase at a fraction of the span was clearing seats at a *larger* test distance
  // and still looking through the tube. Here the eye is placed at the same distance the
  // search was given.
  for (const char* name : {"torus", "klein"}) {
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    const view::Bounds rb = rest.bounds();
    const float span =
        std::max({rb.maxX - rb.minX, rb.maxY - rb.minY, rb.maxZ - rb.minZ});
    const float eyeDistance = std::max(2.0f, 0.4f * span);
    int checked = 0;
    int unoccluded = 0;
    for (const SurfaceSeat& seat : rest.seats()) {
      const view::Vec3 travel = byQuat(seat.quat, view::Vec3{1, 0, 0});
      const SlideOffset offset = alignSlideU(v, seat.cell, travel, eyeDistance);
      SurfacePose pose;
      pose.slideU = offset.u;
      pose.slideV = offset.v;
      const PlaySurface turned = PlaySurface::build(v, pose);
      const SurfaceSeat* fixed = nullptr;
      for (const SurfaceSeat& s : turned.seats()) {
        if (s.cell == seat.cell) fixed = &s;
      }
      REQUIRE(fixed != nullptr);
      ++checked;
      // The search's own shared primitives, against the shape it actually turned: the eye
      // direction is the camera's and the achieved distance is measured at it (M17.20).
      const view::Vec3 toEye =
          chaseEyeDirection(fixed->normal, travel, kDefaultFollowLift);
      const float achieved = clearEyeDistance(turned, fixed->centre, toEye, eyeDistance);
      if (achieved >= eyeDistance - 1e-3f) ++unoccluded;
    }
    CAPTURE(name, checked, unoccluded);
    // A Klein bottle passes through itself, so a cell or two can have a nearer sheet over
    // them whatever the slide; the orientable torus must be clear everywhere.
    CHECK(unoccluded >= (std::string(name) == "torus" ? checked : checked * 9 / 10));
  }
}

TEST_CASE("clearEyeDistance clamps to the last clear distance along the eye",
          "[render]") {
  // M17.20: the anti-clip must be able to pull the camera *closer* when the nominal
  // distance reaches past the shape, rather than trusting a fixed distance. Three cases:
  // clearly unblocked returns the nominal maximum, a direction that hits the far wall
  // before the maximum returns something strictly between, and one already blocked at the
  // minimum returns the minimum so the caller has a defined place to put the camera.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const PlaySurface surf = PlaySurface::build(v);
  REQUIRE_FALSE(surf.seats().empty());
  const SurfaceSeat& a = surf.seats().front();
  // The seat whose centre is farthest from `a`: the chord runs through the shape, so an
  // eye beyond it has that tile between it and `a`.
  const SurfaceSeat* far = &a;
  for (const SurfaceSeat& s : surf.seats()) {
    if (dist(s.centre, a.centre) > dist(far->centre, a.centre)) far = &s;
  }
  const float chord = dist(far->centre, a.centre);
  REQUIRE(chord > 0.5f);

  // Case 1: straight out along the surface normal, a short way - nothing in the way.
  const float clear = clearEyeDistance(surf, a.centre, a.normal, 0.2f, 0.01f);
  CHECK_THAT(clear, WithinAbs(0.2f, 1e-3f));

  // Case 2: toward the far seat and beyond it - blocked, but there is clear air first.
  const view::Vec3 through = view::normalize(far->centre - a.centre);
  const float partial = clearEyeDistance(surf, a.centre, through, 1.5f * chord, 0.01f);
  CHECK(partial > 0.01f);
  CHECK(partial < 1.5f * chord);

  // Case 3: the minimum itself already reaches past the far seat - nothing is clear.
  const float atMin = 1.2f * chord;
  const float none = clearEyeDistance(surf, a.centre, through, 2.0f * chord, atMin);
  CHECK_THAT(none, WithinAbs(atMin, 1e-3f));
}

TEST_CASE("a traced chase on a glued ring stays clear along the whole move", "[render]") {
  // M17.20 regression. The old anti-clip scored a *different* eye direction than the
  // camera drew (the raw normal instead of the orthogonalised one) and, when the nominal
  // distance was blocked, fell through to an unchecked best-facing rotation (root causes
  // 1 and 2). The two property tests above could not see it because they recomputed the
  // search's own formula and fed it a travel read off a seat's frame - perpendicular to
  // that seat's normal by construction, the one case where the two formulas agree. This
  // traces a real path (the a1-a5 rook slide the repro uses) and, at every sampled `t`,
  // runs the real search and checks the drawn sample against the camera's own shared
  // primitives.
  //
  // Torus only. On the self-intersecting Klein bottle the whole move (a1-a5) runs along
  // the rank seam - the figure-eight's pinch - where *no* searched rotation has a clear
  // line to the nominal distance at all (measured: the best achievable over the whole
  // sweep is ~0.5, the clamp floor, versus 0.9*eyeDistance required). That is the surface
  // passing through itself, not a search defect, and it is what `clearEyeDistance`'s
  // clamp exists to limit; klein is covered per-cell by "the chase align clears the shape
  // at the camera's own distance" above. The orientable torus, which is the reported
  // repro, must be clear everywhere.
  for (const char* name : {"torus"}) {
    CAPTURE(name);
    const VariantSpec& v = *new VariantSpec(test::loadVariant(name));
    const PlaySurface rest = PlaySurface::build(v);
    const view::Bounds rb = rest.bounds();
    const float span =
        std::max({rb.maxX - rb.minX, rb.maxY - rb.minY, rb.maxZ - rb.minZ});
    const float eyeDistance = std::max(2.0f, 0.7f * span);
    const float lift = kDefaultFollowLift;

    auto session = app::Session::create(test::loadVariant(name));
    REQUIRE(session.has_value());
    app::Session& s = **session;
    const CellId a1 = v.dims.toCell(Coord::of({0, 0}));
    const CellId a5 = v.dims.toCell(Coord::of({0, 4}));
    const Move* move = nullptr;
    for (const Move& m : s.game().legalMoves()) {
      if (m.from == a1 && m.to == a5) move = &m;
    }
    REQUIRE(move != nullptr);
    const Piece mover = s.game().position().at(a1);
    const view::MovePath path =
        view::tracePath(v, s.game().position(), mover.type, mover.colorOf(), *move);
    REQUIRE_FALSE(path.steps.empty());

    const auto nearest = [&](const PlaySurface& surf, const view::Vec3& p) {
      CellId best = kInvalidCell;
      float bestD = 1e30f;
      for (const SurfaceSeat& st : surf.seats()) {
        const float d = dist(st.centre, p);
        if (d < bestD) {
          bestD = d;
          best = st.cell;
        }
      }
      return best;
    };

    constexpr int kSweep = 48;
    for (int i = 0; i <= kSweep; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(kSweep);
      const SurfaceMoveSample here = surfaceMoveSample(path, rest, t);
      const CellId cell = nearest(rest, here.position);
      REQUIRE(cell != kInvalidCell);
      // The real chase search: it scores each candidate against that candidate's own
      // travel on the traced path, and at the moving sample the camera actually draws
      // (M17.20).
      const SlideOffset off = alignSlideU(v, cell, path, t, eyeDistance, lift);
      SurfacePose pose;
      pose.slideU = off.u;
      pose.slideV = off.v;
      const PlaySurface turned = PlaySurface::build(v, pose);
      const SurfaceMoveSample sample = surfaceMoveSample(path, turned, t);
      view::Vec3 travelCam =
          surfaceMoveSample(path, turned, std::min(1.0f, t + 0.10f)).position -
          surfaceMoveSample(path, turned, std::max(0.0f, t - 0.10f)).position;
      if (view::length(travelCam) < 1e-5f) travelCam = sample.normal;
      const view::Vec3 dir = chaseEyeDirection(sample.normal, travelCam, lift);
      const float achieved = clearEyeDistance(turned, sample.position, dir, eyeDistance);
      CAPTURE(t, off.u, off.v, achieved, eyeDistance);
      CHECK(achieved >= 0.9f * eyeDistance);
    }
  }
}

TEST_CASE("a V slide keeps the Klein rank seam whole", "[render]") {
  // Measured on the Klein bottle's rank seam: sliding U grew the tile-corner gap to about
  // ten times an ordinary gap, while sliding V kept it near one. That is why only V is
  // offered on a non-orientable board.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  constexpr int kSide = kSurfaceSubdiv + 1;
  const auto rankRowGap = [&](const PlaySurface& s, int f, int r0, int r1, bool mirror) {
    const SurfacePatch& a = patchAt(s, v, f, r0);
    const SurfacePatch& b = patchAt(s, v, r1 < 0 ? nx - 1 - f : f, r1 < 0 ? 0 : r1);
    float gap = 0.0f;
    for (int i = 0; i < kSide; ++i) {
      const int bi = mirror ? kSide - 1 - i : i;
      gap = std::max(gap, dist(cornerOf(a, i, kSurfaceSubdiv), cornerOf(b, bi, 0)));
    }
    return gap;
  };
  for (float slide : {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f}) {
    SurfacePose pose;
    pose.slideV = slide;
    const PlaySurface s = PlaySurface::build(v, pose);
    const float ordinary = rankRowGap(s, 0, 2, 3, false);
    for (int f = 0; f < nx; ++f) {
      const float seam = rankRowGap(s, f, nz - 1, -1, true);
      CAPTURE(slide, f, seam, ordinary);
      CHECK(seam < 2.0f * ordinary);
    }
  }
}

TEST_CASE("the Klein surface closes its gluing", "[render]") {
  // Before anything about tiles: does the immersion actually join? The variant glues
  // the ranks with a file flip, so the point at rank 1 must be the point at rank 0 with
  // the file coordinate reversed; the files are glued straight, so u+1 is u. A join that
  // reversed the wrong axis, or by the wrong amount, would not match.
  const float eps = 2e-3f;
  for (int i = 0; i <= 8; ++i) {
    const float u = static_cast<float>(i) / 8.0f;
    const OvVec3 rank = derivedSurfaceAt(app::SurfaceKind::Klein, u, 1.0f);
    const OvVec3 back = derivedSurfaceAt(app::SurfaceKind::Klein, 1.0f - u, 0.0f);
    CAPTURE(u, rank.x, rank.y, rank.z, back.x, back.y, back.z);
    CHECK(std::abs(rank.x - back.x) < eps);
    CHECK(std::abs(rank.y - back.y) < eps);
    CHECK(std::abs(rank.z - back.z) < eps);

    const OvVec3 file = derivedSurfaceAt(app::SurfaceKind::Klein, u + 1.0f, 0.5f);
    const OvVec3 here = derivedSurfaceAt(app::SurfaceKind::Klein, u, 0.5f);
    CHECK(std::abs(file.x - here.x) < eps);
    CHECK(std::abs(file.y - here.y) < eps);
    CHECK(std::abs(file.z - here.z) < eps);
  }
}

TEST_CASE("the Klein gluing seams are ordinary cell steps, not gaps", "[render]") {
  // A gluing seam is where the board continues, so the two cells either side must sit
  // about one lattice step apart - not half a board, and not on top of each other.
  // Checked at a range of slides: the seam stays within the ordinary steps' own spread.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("klein"));
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  for (float slide : {0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f}) {
    SurfacePose pose;
    pose.slideU = slide;
    const PlaySurface s = PlaySurface::build(v, pose);
    // The yardstick is the widest *ordinary* step on the axis, not the step next to the
    // seam: a swept figure-eight makes file steps vary by nearly two to one, and the
    // claim is that the seam is no worse than an ordinary cell, not that every cell is
    // the same size.
    float widestU = 0.0f;
    float widestV = 0.0f;
    for (int f = 0; f < nx; ++f) {
      for (int r = 0; r < nz; ++r) {
        if (f + 1 < nx)
          widestU = std::max(
              widestU, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f + 1, r).centre));
        if (r + 1 < nz)
          widestV = std::max(
              widestV, dist(seatAt(s, v, f, r).centre, seatAt(s, v, f, r + 1).centre));
      }
    }
    // The rank seam: (f, nz-1) joins (nx-1-f, 0).
    for (int f = 0; f < nx; ++f) {
      const float seam =
          dist(seatAt(s, v, f, nz - 1).centre, seatAt(s, v, nx - 1 - f, 0).centre);
      CAPTURE(slide, f, seam, widestV);
      CHECK(seam < 1.25f * widestV);
      CHECK(seam > 0.4f * widestV);
    }
    // The file seam: (nx-1, r) joins (0, r).
    for (int r = 0; r < nz; ++r) {
      const float seam = dist(seatAt(s, v, nx - 1, r).centre, seatAt(s, v, 0, r).centre);
      CAPTURE(slide, r, seam, widestU);
      CHECK(seam < 1.25f * widestU);
      CHECK(seam > 0.4f * widestU);
    }
  }
}

TEST_CASE("a followed move's camera beats align, approach, travel, then return",
          "[render]") {
  // M17.19: the piece is held still through the lead-in (Align/Approach) and only starts
  // travelling once the camera has reached it; the return is a separate stage after the
  // move. Pure in elapsed, so a capture that states a time reproduces it.
  constexpr float kAlign = 0.6f, kApproach = 0.8f, kTravel = 1.5f, kReturn = 0.8f;
  const auto beat = [&](float e) {
    return shapeBeat(e, kAlign, kApproach, kTravel, kReturn);
  };

  CHECK(beat(0.0f).stage == ShapeStage::Align);
  CHECK(beat(0.3f).stage == ShapeStage::Align);
  CHECK_THAT(beat(0.3f).local, WithinAbs(0.5f, 1e-4f));  // half-way through align
  CHECK(beat(kAlign).stage == ShapeStage::Approach);     // the boundary falls through
  CHECK_THAT(beat(kAlign).local, WithinAbs(0.0f, 1e-4f));
  CHECK(beat(kAlign + 0.4f).stage == ShapeStage::Approach);
  CHECK(beat(kAlign + kApproach).stage == ShapeStage::Travel);
  // Travel's local tracks the move's own progress.
  CHECK_THAT(beat(kAlign + kApproach + 0.75f).local, WithinAbs(0.5f, 1e-4f));
  CHECK(beat(kAlign + kApproach + kTravel).stage == ShapeStage::Return);
  CHECK(beat(kAlign + kApproach + kTravel + 0.4f).stage == ShapeStage::Return);
  CHECK(beat(kAlign + kApproach + kTravel + kReturn + 0.01f).stage == ShapeStage::Done);
  CHECK(beat(100.0f).stage == ShapeStage::Done);

  // A zero-length stage is skipped rather than producing a division by zero.
  CHECK(shapeBeat(0.0f, 0.0f, 0.5f, 1.0f, 0.5f).stage == ShapeStage::Approach);
  CHECK(shapeBeat(0.0f, 0.0f, 0.0f, 0.0f, 0.0f).stage == ShapeStage::Done);

  // The ease is a smoothstep: flat at both ends, monotone between.
  CHECK_THAT(shapeEase(0.0f), WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(shapeEase(0.5f), WithinAbs(0.5f, 1e-6f));
  CHECK_THAT(shapeEase(1.0f), WithinAbs(1.0f, 1e-6f));
  CHECK(shapeEase(0.1f) < 0.1f);
  CHECK(shapeEase(0.9f) > 0.9f);
}

TEST_CASE("the look-ahead morph sees the camera about to cross the board", "[render]") {
  // M17.19: the morph is driven by looking at where the camera is going. On the torus's
  // inner ring the chase camera's own path crosses the tube; the aligned board clears it.
  const VariantSpec& v = *new VariantSpec(test::loadVariant("torus"));
  const auto cell = [&](int f, int r) { return v.dims.toCell(Coord::of({f, r})); };
  // A three-step rank glide that wraps toward the tube's inner side: on the unaided board
  // the chase camera's own path crosses the tube somewhere along it. Scanning rather than
  // pinning a `t` keeps this from riding on the glide sampler's exact arc.
  view::MovePath path;
  path.from = cell(1, 0);
  path.to = cell(1, 3);
  for (int r = 0; r < 3; ++r) {
    path.steps.push_back(
        {cell(1, r), cell(1, r + 1), view::StepKind::Interior, {}, 0, Side::Max});
  }
  const PlaySurface surf = PlaySurface::build(v);
  const float eye = 6.0f;
  bool clipped = false;
  for (float tt = 0.0f; tt <= 1.0001f; tt += 0.05f) {
    clipped = clipped || followClips(path, surf, tt, 0.06f, eye);
  }
  CHECK(clipped);
  // Turn the board so the followed cell presents its outer face, and the view is clear.
  const view::Vec3 travel{0.0f, 0.0f, 1.0f};
  const SlideOffset off = alignSlideU(v, cell(1, 3), travel, eye);
  SurfacePose pose;
  pose.slideU = off.u;
  pose.slideV = off.v;
  const PlaySurface turned = PlaySurface::build(v, pose);
  CHECK_FALSE(followClips(path, turned, 1.0f, 0.0f, eye));
}

TEST_CASE("the shape's seam rails sit on the flat view's seams", "[render]") {
  // M17.21. A closed two-dimensional shape has no near edge to hang a rim on the way the
  // flat board does, so it draws the seam as a rail along the wrap ring instead - the row
  // of cells where the lattice coordinate wraps from last back to first. The rail has to
  // land on the *same* cells the flat view's `SeamMap` colours and take the same colour,
  // or the two views disagree about where the seam is.
  const view::Theme theme = BoardRenderer{}.theme();
  for (const char* name : {"cylinder", "torus", "mobius", "klein"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
    // A glued 2-D board draws the files then the ranks, so a lattice axis and its screen
    // index are the same number - the assumption the rail's axis argument rests on.
    REQUIRE(cfg.screenAxes.size() >= 2);
    REQUIRE(cfg.screenAxes[0] == 0);
    REQUIRE(cfg.screenAxes[1] == 1);
    const view::SeamMap seams = view::SeamMap::build(v, cfg, theme);
    const PlaySurface surf = PlaySurface::build(v);
    REQUIRE_FALSE(surf.empty());

    for (const SurfaceSeat& seat : surf.seats()) {
      const Coord co = v.dims.toCoord(seat.cell);
      for (int axis = 0; axis < 2; ++axis) {
        // What the flat view says here: a glued seam on this axis at its *Min* end, which
        // is the wrap edge (the Max end is the same physical seam one cell round).
        bool gluedMin = false;
        view::Rgba want{};
        for (const view::SeamFace& f : seams.at(seat.cell)) {
          if (f.screenAxis == static_cast<std::uint8_t>(axis) && f.side == Side::Min &&
              f.kind == view::SeamKind::Glued) {
            gluedMin = true;
            want = f.color;
          }
        }
        // The wrap ring is the coordinate-0 row, and a rail exists only where that row is
        // really a glued seam. A mirror leads nowhere and gets nothing.
        const int coord = axis == 0 ? co[0] : co[1];
        const bool onRing = gluedMin && coord == 0;
        for (int i = 0; i < kSurfaceSubdiv + 1; ++i) {
          for (int j = 0; j < kSurfaceSubdiv + 1; ++j) {
            const SurfaceRail r = surfaceRail(seams, seat.cell, axis, i, j);
            const bool edge = (axis == 0 ? i : j) == 0;
            CAPTURE(seat.file, seat.rank, axis, i, j, onRing, edge);
            if (onRing && edge) {
              CHECK(r.weight == 1.0f);
              CHECK(r.color.r == want.r);
              CHECK(r.color.g == want.g);
              CHECK(r.color.b == want.b);
            } else {
              CHECK(r.weight == 0.0f);
            }
          }
        }
      }
    }
  }
}

TEST_CASE("the shape's coordinate rings are the two the rails mark", "[render]") {
  // M17.21. With no edge to label, the shape labels the same two reference rings the
  // rails colour: the file letters along the rank-0 ring, the rank numbers along the
  // file-0 ring. Eight labels each on an ordinary 8x8 glued board, matching the flat
  // view's count.
  for (const char* name : {"cylinder", "torus", "mobius", "klein"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const PlaySurface surf = PlaySurface::build(v);
    REQUIRE_FALSE(surf.empty());
    const int nx = static_cast<int>(v.dims.extent(0));
    const int nz = static_cast<int>(v.dims.extent(1));
    int fileLabels = 0;
    int rankLabels = 0;
    for (const SurfaceSeat& seat : surf.seats()) {
      if (seat.rank == 0) ++fileLabels;
      if (seat.file == 0) ++rankLabels;
      // Every seat a label can land on has a real surface point and a normal to push the
      // text out along.
      CHECK(view::length(seat.normal) > 0.9f);
    }
    CHECK(fileLabels == nx);
    CHECK(rankLabels == nz);
  }
}

#endif  // CB_HAVE_IMGUI
