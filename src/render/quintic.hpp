// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
//
// The Calabi-Yau quintic cross-section, shared by the main menu's decoration and the
// `t6` overture.
//
// It is here, rather than a private copy in each caller, because the T6 overture's whole
// claim is that the shape it settles into is the *same object* the title screen has been
// drawing since the game started - and two copies of a formula drift. `deco.cpp` wraps
// this in its own `Vec3`, `overture_scene.cpp` in its `OvVec3`; neither owns the maths.
#include <array>
#include <cmath>

namespace cb::render {

/// The fivefold symmetry the quintic is named for.
inline constexpr int kQuinticN = 5;
/// The angle the two imaginary parts are mixed at to choose which reads as "up". Half is
/// the standard orientation.
inline constexpr float kQuinticAlpha = 0.5f;

/// z1 = e^(2*pi*i*k1/n) (cos a)^(2/n), z2 = e^(2*pi*i*k2/n) (sin a)^(2/n), a = x + iy,
/// drawn as (Re z1, Re z2, Im z1 cos alpha + Im z2 sin alpha). The standard picture, not
/// an impression of one.
inline std::array<float, 3> quinticPoint(int k1, int k2, float x, float y) {
  constexpr float kPi = 3.14159265358979f;
  const float n = static_cast<float>(kQuinticN);
  const auto complexPow = [](float re, float im, float p, float& outRe, float& outIm) {
    const float r = std::hypot(re, im);
    const float th = std::atan2(im, re);
    const float rp = std::pow(r, p);
    outRe = rp * std::cos(th * p);
    outIm = rp * std::sin(th * p);
  };
  const float cRe = std::cos(x) * std::cosh(y);
  const float cIm = -std::sin(x) * std::sinh(y);
  const float sRe = std::sin(x) * std::cosh(y);
  const float sIm = std::cos(x) * std::sinh(y);
  float aRe = 0.0f;
  float aIm = 0.0f;
  float bRe = 0.0f;
  float bIm = 0.0f;
  complexPow(cRe, cIm, 2.0f / n, aRe, aIm);
  complexPow(sRe, sIm, 2.0f / n, bRe, bIm);
  const float p1 = 2.0f * kPi * static_cast<float>(k1) / n;
  const float p2 = 2.0f * kPi * static_cast<float>(k2) / n;
  const float z1r = aRe * std::cos(p1) - aIm * std::sin(p1);
  const float z1i = aRe * std::sin(p1) + aIm * std::cos(p1);
  const float z2r = bRe * std::cos(p2) - bIm * std::sin(p2);
  const float z2i = bRe * std::sin(p2) + bIm * std::cos(p2);
  return {z1r, z2r, z1i * std::cos(kQuinticAlpha) + z2i * std::sin(kQuinticAlpha)};
}

}  // namespace cb::render
