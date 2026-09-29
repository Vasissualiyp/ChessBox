// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/camera.hpp"

#include <limits>

namespace cb::view {

Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 out{};
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      float sum = 0;
      for (int k = 0; k < 4; ++k)
        sum += a[static_cast<std::size_t>(k * 4 + r)] *
               b[static_cast<std::size_t>(c * 4 + k)];
      out[static_cast<std::size_t>(c * 4 + r)] = sum;
    }
  }
  return out;
}

Vec3 transformPoint(const Mat4& m, const Vec3& p) {
  const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
  const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
  const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
  const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
  const float inv = w != 0 ? 1.0f / w : 1.0f;
  return {x * inv, y * inv, z * inv};
}

Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
  const Vec3 f = normalize(center - eye);
  const Vec3 s = normalize(cross(f, up));
  const Vec3 u = cross(s, f);
  Mat4 m{};
  m[0] = s.x;
  m[4] = s.y;
  m[8] = s.z;
  m[12] = -dot(s, eye);
  m[1] = u.x;
  m[5] = u.y;
  m[9] = u.z;
  m[13] = -dot(u, eye);
  m[2] = -f.x;
  m[6] = -f.y;
  m[10] = -f.z;
  m[14] = dot(f, eye);
  m[3] = 0;
  m[7] = 0;
  m[11] = 0;
  m[15] = 1;
  return m;
}

Mat4 perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
  const float t = std::tan(fovYRadians * 0.5f);
  Mat4 m{};
  m[0] = 1.0f / (aspect * t);
  // Negative so that clip-space Y points down, as Vulkan expects; doing it here means
  // no viewport flip and no surprise mirror later.
  m[5] = -1.0f / t;
  m[10] = farZ / (nearZ - farZ);
  m[11] = -1.0f;
  m[14] = (nearZ * farZ) / (nearZ - farZ);
  return m;
}

OrbitCamera OrbitCamera::frame(const Bounds& b, float aspect, float headroom) {
  OrbitCamera cam;
  cam.target = Vec3{b.centerX(), b.centerY(), b.centerZ() + headroom * 0.5f};

  // Fit exactly rather than by the usual bounding-sphere approximation, which is what
  // was clipping the near rank: the board is a wide flat slab seen at a steep angle, so
  // a sphere around it is a poor stand-in for what actually reaches the frustum edges.
  //
  // Moving the camera straight back along its own view direction changes a point's
  // depth and nothing else, so the exact distance to add can be solved in one pass over
  // the corners instead of iterated.
  const float t = std::tan(cam.fovY * 0.5f);
  // The bounds describe cell *centres*, but a cell is half a cell wide either side of
  // its centre - without this margin the near rank is clipped.
  constexpr float kCellMargin = 0.55f;
  const float minX = b.minX - kCellMargin;
  const float maxX = b.maxX + kCellMargin;
  const float minY = b.minY - kCellMargin;
  const float maxY = b.maxY + kCellMargin;
  const float corners[8][3] = {{minX, minY, b.minZ},
                               {maxX, minY, b.minZ},
                               {minX, maxY, b.minZ},
                               {maxX, maxY, b.minZ},
                               {minX, minY, b.maxZ + headroom},
                               {maxX, minY, b.maxZ + headroom},
                               {minX, maxY, b.maxZ + headroom},
                               {maxX, maxY, b.maxZ + headroom}};

  const float radius = std::max(
      1.0f, length(Vec3{b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ}) * 0.5f);
  cam.distance = radius / t;  // a starting point; the pass below makes it exact

  const Vec3 eye = cam.eye();
  const Vec3 forward = normalize(cam.target - eye);
  const Vec3 right = normalize(cross(forward, Vec3{0, 0, 1}));
  const Vec3 up = cross(right, forward);

  float extra = 0.0f;
  for (const auto& c : corners) {
    const Vec3 rel = Vec3{c[0], c[1], c[2]} - eye;
    const float depth = dot(rel, forward);
    const float x = std::abs(dot(rel, right));
    const float y = std::abs(dot(rel, up));
    extra = std::max(extra, x / (aspect * t) - depth);
    extra = std::max(extra, y / t - depth);
  }
  // A little air so nothing sits flush against the edge of the frame.
  cam.distance = (cam.distance + std::max(0.0f, extra)) * 1.04f;
  cam.farZ = cam.distance + radius * 4.0f + 20.0f;
  return cam;
}

Vec3 OrbitCamera::eye() const {
  const float cp = std::cos(pitch);
  return target +
         Vec3{std::sin(yaw) * cp, -std::cos(yaw) * cp, std::sin(pitch)} * distance;
}

Mat4 OrbitCamera::viewProj(float aspect) const {
  // Z is the board's "up" for 3-D boards, and the extra axes lay out in X and Y, so the
  // camera treats Z as up too - looking at a 2-D board then gives the familiar
  // over-the-table view with no special case.
  return multiply(perspective(fovY, aspect, nearZ, farZ),
                  lookAt(eye(), target, Vec3{0, 0, 1}));
}

OrbitCamera::Ray OrbitCamera::pickRay(float px, float py, float width,
                                      float height) const {
  // Reconstruct the ray from the camera basis rather than by inverting the matrix:
  // fewer operations, no near-singular cases, and it is obvious what it does.
  const Vec3 e = eye();
  const Vec3 forward = normalize(target - e);
  const Vec3 right = normalize(cross(forward, Vec3{0, 0, 1}));
  const Vec3 up = cross(right, forward);

  const float aspect = width > 0 ? width / height : 1.0f;
  const float t = std::tan(fovY * 0.5f);
  // Pixel centre to normalised device coordinates; Y is flipped because the image's
  // origin is at the top left.
  const float ndcX = (px + 0.5f) / width * 2.0f - 1.0f;
  const float ndcY = 1.0f - (py + 0.5f) / height * 2.0f;

  const Vec3 dir = normalize(forward + right * (ndcX * t * aspect) + up * (ndcY * t));
  return Ray{e, dir};
}

int pickBox(const OrbitCamera::Ray& ray, const std::vector<Placement>& boxes,
            const Vec3& halfExtent) {
  int best = -1;
  float bestT = std::numeric_limits<float>::max();
  const Vec3 inv{ray.direction.x != 0 ? 1.0f / ray.direction.x : 0,
                 ray.direction.y != 0 ? 1.0f / ray.direction.y : 0,
                 ray.direction.z != 0 ? 1.0f / ray.direction.z : 0};

  for (std::size_t i = 0; i < boxes.size(); ++i) {
    const Placement& p = boxes[i];
    // Standard slab test, skipping axes the ray is parallel to.
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();
    const float center[3]{p.x, p.y, p.z};
    const float half[3]{halfExtent.x, halfExtent.y, halfExtent.z};
    const float origin[3]{ray.origin.x, ray.origin.y, ray.origin.z};
    const float d[3]{ray.direction.x, ray.direction.y, ray.direction.z};
    const float invd[3]{inv.x, inv.y, inv.z};

    bool hit = true;
    for (int a = 0; a < 3; ++a) {
      if (d[a] == 0.0f) {
        if (origin[a] < center[a] - half[a] || origin[a] > center[a] + half[a]) {
          hit = false;
          break;
        }
        continue;
      }
      float t1 = (center[a] - half[a] - origin[a]) * invd[a];
      float t2 = (center[a] + half[a] - origin[a]) * invd[a];
      if (t1 > t2) std::swap(t1, t2);
      tMin = std::max(tMin, t1);
      tMax = std::min(tMax, t2);
      if (tMin > tMax) {
        hit = false;
        break;
      }
    }
    if (hit && tMin < bestT) {
      bestT = tMin;
      best = static_cast<int>(i);
    }
  }
  return best;
}

}  // namespace cb::view
