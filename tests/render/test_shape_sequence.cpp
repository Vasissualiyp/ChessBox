// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shape-follow choreography as a pure function of elapsed time (M12.6). The whole
// point of the marketing runner is that a clip of a move can be requested frame by frame
// and reproduce; that works only if the evaluator reads no clock and no previous frame.
// These tests drive a real torus session into a followed move and check the evaluator
// against itself - the property the file diff relies on.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI

#include <filesystem>
#include <memory>
#include <string>

#include "app/shell.hpp"
#include "render/play_surface.hpp"
#include "render/shape_sequence.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

std::unique_ptr<app::Shell> makeShell() {
  auto settings = std::filesystem::path(CB_BINARY_DIR) / "m126-shape-seq.conf";
  std::filesystem::remove(settings);
  auto shell = app::Shell::create({"torus"}, settings);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  shell->dismissWelcome();
  return shell;
}

/// Whether two sequence states agree to the bit - every float the camera and the board
/// pose are made of. This is stricter than "looks the same": it is what a frame diff
/// sees.
bool identical(const render::ShapeMoveSequence& a, const render::ShapeMoveSequence& b) {
  return a.active == b.active && a.elapsed == b.elapsed && a.camera.yaw == b.camera.yaw &&
         a.camera.pitch == b.camera.pitch && a.camera.distance == b.camera.distance &&
         a.camera.roll == b.camera.roll && a.camera.target.x == b.camera.target.x &&
         a.camera.target.y == b.camera.target.y &&
         a.camera.target.z == b.camera.target.z && a.offset.u == b.offset.u &&
         a.offset.v == b.offset.v;
}

}  // namespace

TEST_CASE("the shape choreography is the same state at the same elapsed time",
          "[render][shape-sequence]") {
  auto shell = makeShell();
  REQUIRE(shell->startGame("torus").has_value());
  app::Settings& st = shell->settings();
  st.geometryView = true;
  st.cameraMode = "route";
  st.followStrength = 1.0f;
  st.shapeFollow = "chase";
  shell->applySettings();

  REQUIRE(shell->session()->playMoveText("a1a5").has_value());
  const view::MovePath& path = shell->session()->animation().path();
  const float travel = shell->session()->animation().duration();
  REQUIRE(travel > 0.0f);

  SECTION("two calls at the same time are bit-identical") {
    const render::ShapeMoveSequence a =
        render::simulateShapeSequence(*shell, path, travel, 0.9f);
    const render::ShapeMoveSequence b =
        render::simulateShapeSequence(*shell, path, travel, 0.9f);
    CHECK(identical(a, b));
  }

  SECTION("out-of-order calls do not depend on what ran before them") {
    const render::ShapeMoveSequence forward =
        render::simulateShapeSequence(*shell, path, travel, 1.4f);
    (void)render::simulateShapeSequence(*shell, path, travel, 2.6f);
    (void)render::simulateShapeSequence(*shell, path, travel, 0.2f);
    const render::ShapeMoveSequence again =
        render::simulateShapeSequence(*shell, path, travel, 1.4f);
    CHECK(identical(forward, again));
  }

  SECTION("the sequence actually moves between the lead-in and the travel") {
    // A guard against a trivially-constant evaluator passing the tests above.
    const render::ShapeMoveSequence start =
        render::simulateShapeSequence(*shell, path, travel, 0.0f);
    const render::ShapeMoveSequence middle =
        render::simulateShapeSequence(*shell, path, travel, 1.6f);
    CHECK_FALSE(identical(start, middle));
  }

  SECTION("the settle time is at least the lead-in plus the move") {
    render::SurfaceCache cache;
    const float settle = render::shapeSequenceSettleSeconds(*shell, path, travel, &cache);
    CHECK(settle >= render::shapeLeadSeconds(st) + travel - 1e-3f);
  }
}

#endif  // CB_HAVE_IMGUI
