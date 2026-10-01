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
      // +X follows the files: stepping one cell along the file axis moves that way.
      const Coord co = v.dims.toCoord(t.cell);
      const int f =
          co.c[0] + 1 < static_cast<int>(v.dims.extent(0)) ? co.c[0] + 1 : co.c[0] - 1;
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
    CHECK_THAT(dist(surfaceMoveSample(path, surf, 0.5f).position, mid->centre),
               WithinAbs(0.0f, 1e-3f));
    for (float t = 0.0f; t <= 1.0001f; t += 0.1f) {
      CHECK(view::length(surfaceMoveSample(path, surf, t).normal) > 0.9f);
    }
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

#endif  // CB_HAVE_IMGUI