// SPDX-License-Identifier: GPL-3.0-or-later
//
// The trailer's resting camera drift (M13.9 revision): it must stay small, smooth and
// switch-offable, because the complaint it answers was a trailer camera that jittered and
// rotated far too much.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

#include "view/trailer_camera.hpp"

using namespace cb::view;

TEST_CASE("steady sway never strays more than a few degrees", "[unit][view][trailer]") {
  OrbitCamera base;
  base.yaw = 0.4f;
  base.pitch = 0.9f;
  const SwayConfig cfg;
  float maxYaw = 0.0f;
  float maxPitch = 0.0f;
  for (float t = 0.0f; t < 400.0f; t += 0.25f) {
    const OrbitCamera c = steadySway(base, t, 1.0f, cfg);
    maxYaw = std::max(maxYaw, std::abs(c.yaw - base.yaw));
    maxPitch = std::max(maxPitch, std::abs(c.pitch - base.pitch));
  }
  CHECK(maxYaw <= cfg.yawAmplitude + 1e-4f);
  CHECK(maxYaw >
        0.5f * cfg.yawAmplitude);  // it does move: a still image is the other mode
  CHECK(maxPitch <= cfg.pitchAmplitude + 1e-4f);
  // "Very little" is a number: under 6 degrees of yaw in the worst case.
  CHECK(maxYaw < 0.105f);
}

TEST_CASE("steady sway at zero weight is the base camera exactly",
          "[unit][view][trailer]") {
  OrbitCamera base;
  base.yaw = 1.3f;
  base.pitch = 0.7f;
  base.distance = 12.0f;
  for (float t : {0.0f, 3.3f, 17.0f, 99.0f}) {
    const OrbitCamera c = steadySway(base, t, 0.0f);
    CHECK(c.yaw == base.yaw);
    CHECK(c.pitch == base.pitch);
    CHECK(c.distance == base.distance);
  }
}

TEST_CASE("steady sway is smooth: no frame steps further than a hair",
          "[unit][view][trailer]") {
  OrbitCamera base;
  const float dt = 1.0f / 60.0f;
  OrbitCamera prev = steadySway(base, 0.0f, 1.0f);
  float worst = 0.0f;
  for (float t = dt; t < 120.0f; t += dt) {
    const OrbitCamera c = steadySway(base, t, 1.0f);
    worst = std::max({worst, std::abs(c.yaw - prev.yaw), std::abs(c.pitch - prev.pitch)});
    prev = c;
  }
  CHECK(worst < 2e-4f);  // ~0.01 degrees per frame at 60 fps
}

TEST_CASE("steady sway leaves target, distance and lens alone", "[unit][view][trailer]") {
  OrbitCamera base;
  base.target = {1.0f, 2.0f, 3.0f};
  base.distance = 9.0f;
  const OrbitCamera c = steadySway(base, 7.0f, 1.0f);
  CHECK(c.target.x == 1.0f);
  CHECK(c.target.y == 2.0f);
  CHECK(c.target.z == 3.0f);
  CHECK(c.distance == 9.0f);
  CHECK(c.fovY == base.fovY);
}
