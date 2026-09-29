// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "view/layout.hpp"

namespace cb::view {

// The camera lives in the view layer rather than in the renderer: it is presentation
// maths with no Vulkan in it, so framing and picking can be tested - and driven by a
// front end - on a machine with no graphics device at all.

/// Column-major 4x4, matching what Vulkan expects from a uniform or push constant.
using Mat4 = std::array<float, 16>;

struct Vec3 {
  float x{0}, y{0}, z{0};
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator+(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator*(const Vec3& a, float s) {
  return {a.x * s, a.y * s, a.z * s};
}
inline float dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& a) {
  return std::sqrt(dot(a, a));
}
inline Vec3 normalize(const Vec3& a) {
  const float l = length(a);
  return l > 0 ? a * (1.0f / l) : a;
}

Mat4 multiply(const Mat4& a, const Mat4& b);
Vec3 transformPoint(const Mat4& m, const Vec3& p);

/// Right-handed look-at.
Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up);

/// Perspective projection with Vulkan's conventions: depth in [0,1] and Y down in
/// clip space, so no flip is needed anywhere else.
Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ);

/// An orbit camera, which is the only control scheme that makes sense for a board you
/// are looking at rather than standing in.
struct OrbitCamera {
  Vec3 target{};
  float distance{10.0f};
  float yaw{0.6f};
  float pitch{0.9f};
  float fovY{0.9f};
  float nearZ{0.1f};
  float farZ{1000.0f};

  /// Frame the whole laid-out scene, whatever its dimensionality.
  static OrbitCamera frame(const Bounds& b);

  [[nodiscard]] Vec3 eye() const;
  [[nodiscard]] Mat4 viewProj(float aspect) const;

  /// Screen-space pick ray. `px`,`py` are pixel coordinates with the origin at the
  /// top left, matching the rendered image.
  struct Ray {
    Vec3 origin;
    Vec3 direction;
  };
  [[nodiscard]] Ray pickRay(float px, float py, float width, float height) const;
};

/// Nearest box hit along a ray, or -1. Boxes are the laid-out cells, which are all
/// axis-aligned - so picking is an exact slab test rather than a depth-buffer readback,
/// and it works with no GPU at all, which is what makes interaction testable headlessly.
int pickBox(const OrbitCamera::Ray& ray, const std::vector<Placement>& boxes,
            const Vec3& halfExtent);

}  // namespace cb::view
