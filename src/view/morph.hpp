// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>

namespace cb::view {

/// The base speed of a flat<->shape morph's own ramp (M18.6): at 1.0 the live value
/// crosses the whole way in half a second, and a per-caller speed multiplies that rate.
/// It lives here, one layer below both the renderer that poses the board and the app
/// shell that drives the main-menu attract sequence, so the two cannot drift apart
/// (M13.9).
inline constexpr float kFormMorphRate = 2.0f;

/// One frame's advance of a linear morph: move `formed` toward `target` by
/// `dt * kFormMorphRate * formSpeed`, stopping exactly on the target rather than
/// approaching it asymptotically, so the caller settles in a definite time and a settle
/// frame is exact (M17.24). Pure, so the per-frame step is testable without a live
/// window. `formSpeed` of 1 is the shipped pacing; larger is faster, smaller slower.
[[nodiscard]] inline float advanceFormMorph(float formed, float target, float dt,
                                            float formSpeed) noexcept {
  const float step = dt * kFormMorphRate * formSpeed;
  if (formed < target) return std::min(target, formed + step);
  if (formed > target) return std::max(target, formed - step);
  return formed;
}

}  // namespace cb::view
