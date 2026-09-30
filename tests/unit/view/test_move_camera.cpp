// SPDX-License-Identifier: GPL-3.0-or-later
//
// The move camera, headless. M11's claim is that a camera reading only `MovePath` and
// `Placement` generalises to every geometry and dimension with no dimension branch; these
// tests pin the pure function, its route decomposition, and the oracle the smoothing is
// measured against.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "io/fen.hpp"
#include "support/variants.hpp"
#include "view/move_camera.hpp"

using namespace cb;
using namespace cb::view;
using Catch::Matchers::WithinAbs;

namespace {

Move slide(const VariantSpec& v, std::initializer_list<int> from,
           std::initializer_list<int> to) {
  Move m;
  m.from = v.dims.toCell(Coord::of(from));
  m.to = v.dims.toCell(Coord::of(to));
  return m;
}

Position board(const VariantSpec& v, const char* fen) {
  auto p = fromFen(v, fen);
  REQUIRE(p.has_value());
  return std::move(*p);
}

/// How the identity test policy would draw: no lead, no pull, linear time, so the pose is
/// exactly the piece's own position - the oracle M11.2 defines.
CameraPolicy identity() {
  CameraPolicy p;
  p.follow = FollowMode::Route;
  p.lead = 0.0f;
  p.pull = 0.0f;
  p.minDistance = 0.0f;
  return p;
}

Vec3 placementOf(const VariantSpec& v, const ViewConfig& cfg,
                 std::initializer_list<int> cell) {
  const CellId id = v.dims.toCell(Coord::of(cell));
  for (const Placement& p : layout(v.dims, cfg)) {
    if (p.cell == id) return {p.x, p.y, p.z};
  }
  FAIL("cell not laid out");
  return {};
}

float dist(const Vec3& a, const Vec3& b) {
  return length(a - b);
}

}  // namespace

TEST_CASE("the policy defaults follow the route and never dolly in", "[unit][view]") {
  const CameraPolicy p;
  CHECK(p.follow == FollowMode::Route);
  CHECK(p.lead > 0.0f);
  CHECK(p.pull >= 0.0f);
  CHECK(p.minDistance > 0.0f);
  CHECK(p.deadline > 0.0f);
}

TEST_CASE("with following off the camera is the settled board framing", "[unit][view]") {
  // The strict no-op: every pre-M11 golden has to keep holding, so `Off` may not move
  // the camera at all, whatever `t` is.
  const VariantSpec v = test::loadVariant("standard");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const std::vector<Placement> placements = layout(v.dims, cfg);
  const Bounds scene = boundsOf(placements);
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath path = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                                  Color::White, slide(v, {0, 0}, {0, 3}));

  CameraPolicy off;
  off.follow = FollowMode::Off;
  const CameraPose a = moveCamera(path, placements, cfg, scene, off, 0.0f);
  const CameraPose b = moveCamera(path, placements, cfg, scene, off, 1.0f);
  CHECK_THAT(dist(a.target, b.target), WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(a.distance - b.distance, WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(a.yaw - b.yaw, WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("the route decomposes into straight runs split at the seams", "[unit][view]") {
  SECTION("a plain slide is one run") {
    const VariantSpec v = test::loadVariant("standard");
    const ViewConfig cfg = ViewConfig::forBoard(v.dims);
    const PieceTypeId rook = v.findPiece("rook");
    const MovePath path = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                                    Color::White, slide(v, {0, 0}, {0, 3}));
    const std::vector<RouteRun> runs = routeRuns(path, layout(v.dims, cfg));
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].endedWith == StepKind::Interior);
    CHECK(runs[0].points.size() == 4);  // four cells of the line
    CHECK_THAT(runs[0].t0, WithinAbs(0.0f, 1e-6f));
    CHECK_THAT(runs[0].t1, WithinAbs(1.0f, 1e-6f));
  }
  SECTION("a wrap is two runs and the first ends at the seam") {
    const VariantSpec v = test::loadVariant("cylinder");
    const ViewConfig cfg = ViewConfig::forBoard(v.dims);
    const PieceTypeId rook = v.findPiece("rook");
    const MovePath path = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                                    Color::White, slide(v, {0, 0}, {6, 0}));
    const std::vector<RouteRun> runs = routeRuns(path, layout(v.dims, cfg));
    REQUIRE(runs.size() == 2);
    CHECK(runs[0].endedWith == StepKind::Portal);
    CHECK(runs[0].t1 < runs[1].t0 + 1e-6f);
  }
}

TEST_CASE("the camera frames the piece exactly under the identity policy",
          "[unit][view]") {
  // The oracle M11.2 defines: at a run boundary, with no lead, no pull and linear time,
  // the target is the piece's own world position. Every smoothing term is a deviation
  // from this, proven to stay inside the safe zone.
  const VariantSpec v = test::loadVariant("standard");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const std::vector<Placement> placements = layout(v.dims, cfg);
  const Bounds scene = boundsOf(placements);
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath path = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                                  Color::White, slide(v, {0, 0}, {0, 3}));

  const CameraPose start = moveCamera(path, placements, cfg, scene, identity(), 0.0f);
  CHECK_THAT(dist(start.target, placementOf(v, cfg, {0, 0})), WithinAbs(0.0f, 1e-4f));
  const CameraPose end = moveCamera(path, placements, cfg, scene, identity(), 1.0f);
  CHECK_THAT(dist(end.target, placementOf(v, cfg, {0, 3})), WithinAbs(0.0f, 1e-4f));
  // Half way along a straight run is half way between the end cells (arc length).
  const CameraPose mid = moveCamera(path, placements, cfg, scene, identity(), 0.5f);
  const Vec3 half = (placementOf(v, cfg, {0, 1}) + placementOf(v, cfg, {0, 2})) * 0.5f;
  CHECK_THAT(dist(mid.target, half), WithinAbs(0.0f, 1e-4f));
}

TEST_CASE("the camera function is pure", "[unit][view]") {
  const VariantSpec v = test::loadVariant("klein");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const std::vector<Placement> placements = layout(v.dims, cfg);
  const Bounds scene = boundsOf(placements);
  const PieceTypeId knight = v.findPiece("knight");
  const MovePath path = tracePath(v, board(v, "3N4/8/8/8/8/8/8/8 w - - 0 1"), knight,
                                  Color::White, slide(v, {3, 7}, {2, 0}));
  const CameraPolicy policy;
  for (int i = 0; i <= 20; ++i) {
    const float t = static_cast<float>(i) / 20.0f;
    const CameraPose a = moveCamera(path, placements, cfg, scene, policy, t);
    const CameraPose b = moveCamera(path, placements, cfg, scene, policy, t);
    CHECK_THAT(dist(a.target, b.target), WithinAbs(0.0f, 0.0f));
    CHECK_THAT(a.distance - b.distance, WithinAbs(0.0f, 0.0f));
    CHECK_THAT(a.yaw - b.yaw, WithinAbs(0.0f, 0.0f));
    CHECK_THAT(a.pitch - b.pitch, WithinAbs(0.0f, 0.0f));
  }
}

TEST_CASE("following keeps the piece inside the board, with lead and pull",
          "[unit][view]") {
  // The safe-zone idea, first order: a followed piece never leaves the drawn scene by
  // more than the lead it is granted, and the camera never dollies inside the floor. This
  // is the invariant the full projection test (M11.7) refines.
  const VariantSpec v = test::loadVariant("torus");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const std::vector<Placement> placements = layout(v.dims, cfg);
  const Bounds scene = boundsOf(placements);
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath path = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                                  Color::White, slide(v, {0, 0}, {6, 0}));
  const CameraPolicy policy;
  const float margin = policy.lead + 0.75f;
  for (int i = 0; i <= 40; ++i) {
    const float t = static_cast<float>(i) / 40.0f;
    const CameraPose p = moveCamera(path, placements, cfg, scene, policy, t);
    CHECK(p.target.x >= scene.minX - margin);
    CHECK(p.target.x <= scene.maxX + margin);
    CHECK(p.target.y >= scene.minY - margin);
    CHECK(p.target.y <= scene.maxY + margin);
    CHECK(p.distance >= policy.minDistance);
  }
}

TEST_CASE("a pose applies to the orbit camera by assignment", "[unit][view]") {
  CameraPose pose;
  pose.target = {1.0f, 2.0f, 3.0f};
  pose.distance = 7.0f;
  pose.yaw = 0.25f;
  pose.pitch = 0.75f;
  const OrbitCamera cam = toOrbit(pose);
  CHECK_THAT(cam.target.x - 1.0f, WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(cam.target.y - 2.0f, WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(cam.target.z - 3.0f, WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(cam.distance - 7.0f, WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(cam.yaw - 0.25f, WithinAbs(0.0f, 1e-6f));
  CHECK_THAT(cam.pitch - 0.75f, WithinAbs(0.0f, 1e-6f));
}
