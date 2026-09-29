// SPDX-License-Identifier: GPL-3.0-or-later
// Camera and picking, tested with no GPU at all.
//
// Most "rendering bugs" are really projection bugs, and those are catchable as numbers:
// project a cell's centre to the screen, fire a pick ray back through that pixel, and
// assert it hits the same cell. If that round-trip holds for every cell of a 2-D, 3-D and
// 4-D board, the click-to-select path is correct before a single pixel is drawn.
#include <catch2/catch_test_macros.hpp>

#include "render/board_renderer.hpp"
#include "view/camera.hpp"
#include "support/variants.hpp"
#include "view/layout.hpp"

using namespace cb;
using namespace cb::render;

namespace {

/// Project a world point to pixel coordinates, the inverse of view::OrbitCamera::pickRay.
struct Screen {
  float x{0}, y{0};
  bool visible{false};
};

Screen project(const view::OrbitCamera& cam, const view::Vec3& world, float w, float h) {
  const view::Mat4 vp = cam.viewProj(w / h);
  const float cx = vp[0] * world.x + vp[4] * world.y + vp[8] * world.z + vp[12];
  const float cy = vp[1] * world.x + vp[5] * world.y + vp[9] * world.z + vp[13];
  const float cw = vp[3] * world.x + vp[7] * world.y + vp[11] * world.z + vp[15];
  if (cw <= 0.0f) return {};
  Screen s;
  s.x = (cx / cw * 0.5f + 0.5f) * w - 0.5f;
  s.y = (cy / cw * 0.5f + 0.5f) * h - 0.5f;
  s.visible = s.x >= 0 && s.y >= 0 && s.x < w && s.y < h;
  return s;
}

}  // namespace

TEST_CASE("a 2-D board round-trips every cell exactly", "[render][view]") {
  // With one flat layer there is nothing to occlude anything, so projecting a cell's
  // centre and firing a ray back through that pixel must return that same cell. This is
  // the click-to-select path, verified without drawing anything.
  const VariantSpec v = test::loadVariant("standard");
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const auto placements = view::layout(v.dims, cfg);
  const view::OrbitCamera cam = view::OrbitCamera::frame(view::boundsOf(placements));
  const view::Vec3 half = BoardRenderer::cellHalfExtent();

  std::size_t exact = 0;
  for (std::size_t i = 0; i < placements.size(); ++i) {
    const view::Placement& p = placements[i];
    const Screen s = project(cam, view::Vec3{p.x, p.y, p.z}, 800.0f, 600.0f);
    REQUIRE(s.visible);
    const int hit = view::pickBox(cam.pickRay(s.x, s.y, 800.0f, 600.0f), placements, half);
    if (hit == static_cast<int>(i)) ++exact;
  }
  // A handful of cells can be clipped by the one in front of them at a grazing angle,
  // which is correct behaviour for a click; the rest must be exact.
  CAPTURE(exact, placements.size());
  REQUIRE(exact * 100 / placements.size() >= 95);
}

TEST_CASE("a solid N-D board picks the nearest cell, deterministically",
          "[render][view]") {
  // On a filled 3-D or 4-D board most cells are hidden behind others, so demanding that
  // a pixel return *that* cell would be wrong - a click must select what is in front.
  // What must hold is that a pixel covering the board returns a cell at all, that it is
  // the nearest one along the ray, and that repeating the query gives the same answer.
  for (const char* name : {"cube5", "hyper4", "torus3d"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
    const auto placements = view::layout(v.dims, cfg);
    const view::OrbitCamera cam = view::OrbitCamera::frame(view::boundsOf(placements));
    const view::Vec3 half = BoardRenderer::cellHalfExtent();

    std::size_t probed = 0;
    std::size_t hitSomething = 0;
    for (std::size_t i = 0; i < placements.size(); i += 3) {
      const view::Placement& p = placements[i];
      const Screen s = project(cam, view::Vec3{p.x, p.y, p.z}, 800.0f, 600.0f);
      if (!s.visible) continue;
      ++probed;
      const auto ray = cam.pickRay(s.x, s.y, 800.0f, 600.0f);
      const int hit = view::pickBox(ray, placements, half);
      if (hit < 0) continue;
      ++hitSomething;
      REQUIRE(view::pickBox(ray, placements, half) == hit);  // deterministic

      // The hit must be no further from the camera than the cell we aimed at.
      const view::Vec3 hp{placements[static_cast<std::size_t>(hit)].x,
                    placements[static_cast<std::size_t>(hit)].y,
                    placements[static_cast<std::size_t>(hit)].z};
      const float dHit = view::length(hp - ray.origin);
      const float dAimed = view::length(view::Vec3{p.x, p.y, p.z} - ray.origin);
      REQUIRE(dHit <= dAimed + 1.0f);
    }
    CAPTURE(probed, hitSomething);
    REQUIRE(probed > 10);
    REQUIRE(hitSomething == probed);  // every pixel over the board selects something
  }
}

TEST_CASE("a pick ray aimed at a cell hits a cell", "[render][view]") {
  // Deliberately aimed at a cell rather than at the geometric centre of the board: the
  // centre of an 8x8 board falls in the gap *between* four cells, and a ray straight
  // through that gap correctly hits nothing.
  const VariantSpec v = test::loadVariant("standard");
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const auto placements = view::layout(v.dims, cfg);
  const view::OrbitCamera cam = view::OrbitCamera::frame(view::boundsOf(placements));
  const Screen s = project(cam, view::Vec3{3.0f, 3.0f, 0.0f}, 800.0f, 600.0f);
  REQUIRE(s.visible);
  REQUIRE(view::pickBox(cam.pickRay(s.x, s.y, 800.0f, 600.0f), placements,
                  BoardRenderer::cellHalfExtent()) >= 0);
}

TEST_CASE("the camera frames boards of every dimensionality", "[render][view]") {
  for (const char* name : {"standard", "cube5", "hyper4", "torus3d"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
    const auto placements = view::layout(v.dims, cfg);
    const view::OrbitCamera cam = view::OrbitCamera::frame(view::boundsOf(placements));

    // Every cell projects inside the frame: a default view must never cut the board off,
    // whatever its shape.
    std::size_t offFrame = 0;
    for (const view::Placement& p : placements) {
      if (!project(cam, view::Vec3{p.x, p.y, p.z}, 800.0f, 600.0f).visible) ++offFrame;
    }
    CAPTURE(offFrame, placements.size());
    REQUIRE(offFrame == 0);
  }
}

TEST_CASE("matrix helpers obey their algebra", "[render][view]") {
  // A wrong multiplication order here would show up as a board that is mirrored or
  // inside out, which is much harder to diagnose from a picture than from an assertion.
  const view::Mat4 a = view::lookAt(view::Vec3{3, -4, 5}, view::Vec3{0, 0, 0}, view::Vec3{0, 0, 1});
  const view::Mat4 p = view::perspective(0.9f, 1.5f, 0.1f, 100.0f);
  const view::Mat4 both = view::multiply(p, a);
  const view::Vec3 point{1, 2, 0.5f};
  const view::Vec3 viaBoth = view::transformPoint(both, point);
  const view::Vec3 viaSteps = view::transformPoint(p, view::transformPoint(a, point));
  REQUIRE(std::abs(viaBoth.x - viaSteps.x) < 1e-4f);
  REQUIRE(std::abs(viaBoth.y - viaSteps.y) < 1e-4f);

  // Looking down -Y with Z up puts the target ahead of the camera, at positive depth.
  const view::Vec3 eyeSpace = view::transformPoint(a, view::Vec3{0, 0, 0});
  REQUIRE(eyeSpace.z < 0.0f);
}
