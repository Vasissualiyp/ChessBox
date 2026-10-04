// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

#include "view/camera.hpp"

using namespace cb;
using Catch::Matchers::WithinAbs;

namespace {

float angleBetween(const view::Vec3& a, const view::Vec3& b) {
  return std::acos(std::clamp(view::dot(a, b), -1.0f, 1.0f));
}

view::Vec3 forwardOf(const view::OrbitCamera& c) {
  view::Vec3 f{}, r{}, u{};
  c.basis(f, r, u);
  return f;
}

}  // namespace

TEST_CASE("slerpCamera ends on its endpoints", "[view]") {
  view::OrbitCamera a;
  a.target = {1.0f, 2.0f, 3.0f};
  a.distance = 9.0f;
  a.yaw = 0.4f;
  a.pitch = 0.7f;
  a.roll = 0.2f;
  view::OrbitCamera b;
  b.target = {-2.0f, 0.5f, 1.0f};
  b.distance = 4.0f;
  b.yaw = -2.1f;
  b.pitch = -0.3f;
  b.roll = -0.9f;

  const view::OrbitCamera at0 = view::slerpCamera(a, b, 0.0f);
  const view::OrbitCamera at1 = view::slerpCamera(a, b, 1.0f);
  for (const auto& [got, want] : {std::pair{at0, a}, std::pair{at1, b}}) {
    CHECK_THAT(view::length(got.eye() - want.eye()), WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(view::length(got.target - want.target), WithinAbs(0.0f, 1e-3f));
    CHECK_THAT(view::length(forwardOf(got) - forwardOf(want)), WithinAbs(0.0f, 1e-3f));
  }
}

TEST_CASE("slerpCamera's target travels a straight line", "[view]") {
  // M17.23: `target` is the authored state and the one that must be interpolated,
  // matching `OrbitCamera`'s own target-primary representation. Interpolating `eye` and
  // back-solving a target - what this used to do - let an in-between camera aim away from
  // both subjects when their targets differ.
  view::OrbitCamera a;
  a.target = {1.0f, -2.0f, 0.5f};
  a.distance = 10.0f;
  a.yaw = 0.3f;
  a.pitch = 0.8f;
  view::OrbitCamera b;
  b.target = {-2.5f, 1.5f, 0.0f};
  b.distance = 4.0f;
  b.yaw = -2.5f;
  b.pitch = -0.6f;
  const view::Vec3 from = a.target;
  const view::Vec3 delta = b.target - from;
  view::Vec3 prevEye = a.eye();
  for (int i = 0; i <= 50; ++i) {
    const float t = static_cast<float>(i) / 50.0f;
    const view::OrbitCamera c = view::slerpCamera(a, b, t);
    CHECK_THAT(view::length(c.target - (from + delta * t)), WithinAbs(0.0f, 1e-3f));
    // The eye is no longer on a straight line - the target is - but it stays a smooth,
    // continuous function of `t`: no step jumps a whole camera's distance.
    if (i > 0) CHECK(view::length(c.eye() - prevEye) < 2.0f);
    prevEye = c.eye();
  }
}

TEST_CASE("slerpCamera keeps both endpoints' subjects in frame", "[view]") {
  // M17.23 regression: the Align/Approach/Return blend
  // (`src/render/shape_sequence.cpp`) always passes two cameras with *different* targets -
  // a far, board-centred player camera and a close, piece-centred chase camera. If the
  // blend lets the aim drift off both, the board leaves the frame entirely; a captured
  // torus clip went flat-background for several frames during Return. Both subjects must
  // stay projectable for the whole blend.
  view::OrbitCamera player;  // settled, far, looking at the board centre
  player.target = {0.0f, 0.0f, 0.0f};
  player.distance = 9.0f;
  player.yaw = 0.5f;
  player.pitch = 0.9f;
  view::OrbitCamera chase;  // close, piece-centred, the real chase magnitudes
  chase.target = {2.2f, -1.0f, 0.0f};
  chase.distance = 3.2f;
  chase.yaw = -2.4f;
  chase.pitch = 0.3f;

  constexpr float kAspect = 16.0f / 9.0f;
  constexpr float kW = 1280.0f;
  constexpr float kH = 720.0f;
  constexpr float kSlack = 0.4f;
  const view::Vec3 offsets[7] = {{0.0f, 0.0f, 0.0f},
                                 {kSlack, 0.0f, 0.0f},
                                 {-kSlack, 0.0f, 0.0f},
                                 {0.0f, kSlack, 0.0f},
                                 {0.0f, -kSlack, 0.0f},
                                 {0.0f, 0.0f, kSlack},
                                 {0.0f, 0.0f, -kSlack}};
  auto anyVisible = [&](const view::OrbitCamera& c, const view::Vec3& p) {
    for (const view::Vec3& o : offsets)
      if (c.project(p + o, kAspect, kW, kH).visible) return true;
    return false;
  };

  for (int i = 0; i <= 100; ++i) {
    const float t = static_cast<float>(i) / 100.0f;
    const view::OrbitCamera c = view::slerpCamera(player, chase, t);
    INFO("t = " << t);
    CHECK(anyVisible(c, player.target));
    CHECK(anyVisible(c, chase.target));
  }
}

TEST_CASE("slerpCamera turns the shortest way, never the long way", "[view]") {
  // The defect this replaces: interpolating the eye *direction* (or yaw/pitch/roll)
  // separately let the view swing through the origin for near-opposite cameras - a 720
  // degree spin. A quaternion slerp turns along the geodesic, so the forward sweeps the
  // short arc and no step is large.
  view::OrbitCamera a;
  a.target = {0.0f, 0.0f, 0.0f};
  a.distance = 8.0f;
  a.yaw = -0.2f;
  a.pitch = 1.2f;  // steeply down
  a.roll = 0.0f;
  view::OrbitCamera b;
  b.target = {0.0f, 0.0f, 0.0f};
  b.distance = 8.0f;
  b.yaw = 2.9f;
  b.pitch = -1.1f;  // steeply up, nearly opposite
  b.roll = 2.0f;

  const float total = angleBetween(forwardOf(a), forwardOf(b));
  float swept = 0.0f;
  float worst = 0.0f;
  view::Vec3 prev = forwardOf(a);
  const int steps = 200;
  for (int i = 1; i <= steps; ++i) {
    const view::Vec3 f =
        forwardOf(view::slerpCamera(a, b, static_cast<float>(i) / steps));
    const float d = angleBetween(prev, f);
    swept += d;
    worst = std::max(worst, d);
    prev = f;
  }
  // The sweep is the geodesic, not the long way round, and it is finely subdivided.
  CHECK_THAT(swept, WithinAbs(total, 0.05f));
  CHECK(worst < 0.2f);
}
