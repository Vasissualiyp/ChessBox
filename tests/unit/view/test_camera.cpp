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

TEST_CASE("slerpCamera's eye travels a straight line", "[view]") {
  view::OrbitCamera a;
  a.target = {0.0f, 0.0f, 0.0f};
  a.distance = 10.0f;
  a.yaw = 0.3f;
  a.pitch = 0.8f;
  view::OrbitCamera b;
  b.target = {0.0f, 0.0f, 0.0f};
  b.distance = 4.0f;
  b.yaw = -2.5f;
  b.pitch = -0.6f;
  const view::Vec3 from = a.eye();
  const view::Vec3 delta = b.eye() - from;
  for (int i = 0; i <= 50; ++i) {
    const float t = static_cast<float>(i) / 50.0f;
    const view::OrbitCamera c = view::slerpCamera(a, b, t);
    CHECK_THAT(view::length(c.eye() - (from + delta * t)), WithinAbs(0.0f, 1e-3f));
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
