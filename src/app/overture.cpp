// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/overture.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace cb::app {
namespace {

/// Exact names, never prefixes: `atomic_torus` is not a longer way of writing `atomic`,
/// and matching it as one would show a flat detonation for the variant whose whole point
/// is that the blast meets geometry that wraps.
constexpr std::array<std::pair<std::string_view, Overture>, 15> kTable{{
    {"standard", Overture::Standard},
    {"cylinder", Overture::Cylinder},
    {"torus", Overture::Torus},
    {"mobius", Overture::Mobius},
    {"klein", Overture::Klein},
    {"mirrorbox", Overture::Mirrorbox},
    {"cube5", Overture::Cube5},
    {"hyper4", Overture::Hyper4},
    {"atomic", Overture::Atomic},
    {"atomic_torus", Overture::AtomicTorus},
    {"mustcapture", Overture::MustCapture},
    {"5d", Overture::Multiverse},
    {"torus3d", Overture::Torus3d},
    {"t6", Overture::T6},
    // Standard chess with a custom per-piece field, which is not a shape at all.
    {"charged", Overture::Standard},
}};

}  // namespace

Overture overtureFor(std::string_view variantName) noexcept {
  for (const auto& [name, o] : kTable) {
    if (name == variantName) return o;
  }
  return Overture::Standard;
}

std::string_view overtureName(Overture o) noexcept {
  switch (o) {
    case Overture::None:
      return "none";
    case Overture::Standard:
      return "standard";
    case Overture::Cylinder:
      return "cylinder";
    case Overture::Torus:
      return "torus";
    case Overture::Mobius:
      return "mobius";
    case Overture::Klein:
      return "klein";
    case Overture::Mirrorbox:
      return "mirrorbox";
    case Overture::Cube5:
      return "cube5";
    case Overture::Hyper4:
      return "hyper4";
    case Overture::Atomic:
      return "atomic";
    case Overture::AtomicTorus:
      return "atomic_torus";
    case Overture::MustCapture:
      return "mustcapture";
    case Overture::Multiverse:
      return "5d";
    case Overture::Torus3d:
      return "torus3d";
    case Overture::T6:
      return "t6";
  }
  return "none";
}

void OverturePlayer::select(std::string_view variantName) {
  select(overtureFor(variantName));
}

void OverturePlayer::select(Overture o) {
  pinned_ = false;
  if (o == Overture::None || o == current_) {
    // Picking the entry already running is not a change - and it cancels one that was
    // waiting, because the player has just told us they want this one after all.
    pending_ = Overture::None;
    return;
  }
  if (phase_ == Phase::Settled) {
    // Already flat, so there is nothing to unwind and no reason to wait.
    current_ = o;
    pending_ = Overture::None;
    phase_ = Phase::Forward;
    t_ = 0.0f;
    dwell_ = 0.0f;
    return;
  }
  // One slot, not a queue: only the latest selection is still of interest, and a player
  // running down the library must not enqueue every entry they pass.
  //
  // The shape on screen is *not* interrupted. It finishes forming, dwells, and unwinds
  // on its own clock, and the new one begins when it reaches the flat board. Cutting
  // straight to the unwind makes the screen answer faster, but it also means a variant
  // glanced at for a second is a variant whose overture nobody ever sees finish.
  pending_ = o;
}

void OverturePlayer::advance(float dt) {
  if (pinned_ || !(dt > 0.0f)) return;
  switch (phase_) {
    case Phase::Forward:
      t_ += dt / kSweep;
      if (t_ >= 1.0f) {
        t_ = 1.0f;
        phase_ = Phase::Held;
        dwell_ = 0.0f;
        // The construction is spent here rather than at the flat end, and that is the
        // whole trick: the two versions of the standard scene are identical at t = 1 by
        // construction, so the flag can drop with nothing visible happening. Dropping
        // it at t = 0 would pop an empty board into a full one during the dwell.
        if (current_ == Overture::Standard) intro_ = false;
      }
      break;
    case Phase::Held:
      dwell_ += dt;
      if (dwell_ >= kHoldFormed) {
        phase_ = Phase::Reverse;
        dwell_ = 0.0f;
      }
      break;
    case Phase::Reverse:
      t_ -= dt / kSweep;
      if (t_ <= 0.0f) {
        t_ = 0.0f;
        phase_ = Phase::Settled;
        dwell_ = 0.0f;
      }
      break;
    case Phase::Settled:
      dwell_ += dt;
      if (dwell_ >= kHoldFlat) {
        if (pending_ != Overture::None) {
          current_ = pending_;
          pending_ = Overture::None;
        }
        phase_ = Phase::Forward;
        dwell_ = 0.0f;
      }
      break;
  }
}

void OverturePlayer::jumpTo(Overture o) noexcept {
  if (o == Overture::None) return;
  pinned_ = false;
  current_ = o;
  pending_ = Overture::None;
  phase_ = Phase::Forward;
  t_ = 0.0f;
  dwell_ = 0.0f;
}

void OverturePlayer::setProgress(float t) noexcept {
  pinned_ = true;
  t_ = std::clamp(t, 0.0f, 1.0f);
  phase_ = t_ >= 1.0f ? Phase::Held : (t_ <= 0.0f ? Phase::Settled : Phase::Forward);
  dwell_ = 0.0f;
}

}  // namespace cb::app
