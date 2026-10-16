// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

#include "view/camera.hpp"

namespace cb::view {

/// How far, and how slowly, the trailer's resting camera drifts (M13.9 revision). Small
/// on purpose: a trailer camera that rotates a lot reads as the player's camera fighting
/// the game, and one that moves per-move reads as jitter.
struct SwayConfig {
  float yawAmplitude{0.07f};    ///< radians, about 4 degrees either way
  float pitchAmplitude{0.02f};  ///< radians, about 1 degree
  float period{40.0f};          ///< seconds for one full yaw cycle
};

/// The trailer's resting camera: `base` with a slow sinusoidal drift added.
///
/// A pure function of the trailer clock `t` and a `weight` in [0,1] that fades the drift
/// in or out (edit mode and the authored camera fade it to 0 so the view is exactly what
/// was set up). It reads no previous frame, so it cannot accumulate error or stutter, and
/// the clock is never reset by a cut, so the drift is continuous across one. The fastest
/// angular speed is `yawAmplitude * 2 * pi / period` - about 0.011 rad/s at the defaults.
[[nodiscard]] inline OrbitCamera steadySway(OrbitCamera base, float t, float weight,
                                            const SwayConfig& cfg = {}) noexcept {
  constexpr float kTwoPi = 6.28318530718f;
  const float w = std::clamp(weight, 0.0f, 1.0f);
  const float period = std::max(1.0f, cfg.period);
  base.yaw += w * cfg.yawAmplitude * std::sin(kTwoPi * t / period);
  // A different, incommensurate period for the pitch so the path is a slow figure, not a
  // line the eye can predict.
  base.pitch = std::clamp(
      base.pitch + w * cfg.pitchAmplitude * std::sin(kTwoPi * t / (period * 1.37f)),
      0.05f, 1.5207963f);
  return base;
}

}  // namespace cb::view
