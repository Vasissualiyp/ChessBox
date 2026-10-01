// SPDX-License-Identifier: GPL-3.0-or-later
//
// The play board as its own shape (M17). Where every cell of a glued two-dimensional
// variant goes once the board stops being a picture of a topology and becomes it.
//
// Three things live here because they must agree: the patch of surface each square is,
// the seat a piece stands on, and the ray a click is tested against. They are all read
// off one sampling of the surface, which is the cheapest way to keep ADR-0011's invariant
// - the pick follows what was drawn - true by construction rather than by care.
#include "render/play_surface.hpp"

#include <algorithm>
#include <cmath>

#include "app/overture.hpp"

namespace cb::render {
namespace {

/// The overture world is Y-up; the board's world is Z-up and the camera orbits about Z.
/// One quarter turn about X takes one to the other. Without it a torus stands on its rim
/// like a wheel and a cylinder lies on its side under a camera that cannot get above
/// either - which is how the surface first arrived.
view::Vec3 upright(const OvVec3& p) {
  return {p.x, -p.z, p.y};
}

view::Vec3 sample(app::SurfaceKind kind, SurfacePose pose, float u, float v) {
  return upright(derivedSurfaceAt(kind, u, v, pose));
}

/// The outward normal at a lattice fraction, from the surface's own tangents.
///
/// The differences are deliberately *unclamped*: every warp in the catalogue is a smooth
/// function of the lattice coordinate well outside [0, 1] - which is also what makes the
/// slide work - so sampling past an edge is the honest tangent, while clamping halves it
/// and leaves the rim corners facing the wrong way.
view::Vec3 normalAt(app::SurfaceKind kind, SurfacePose pose, float u, float v, int nx,
                    int nz) {
  const float hu = 0.2f / static_cast<float>(nx);
  const float hv = 0.2f / static_cast<float>(nz);
  const view::Vec3 du = sample(kind, pose, u + hu, v) - sample(kind, pose, u - hu, v);
  const view::Vec3 dv = sample(kind, pose, u, v + hv) - sample(kind, pose, u, v - hv);
  // Out of the surface, not into it: `cross(dv, du)` is the outward side for every warp
  // in the catalogue, checked on the cylinder where "outward" is not a matter of taste.
  const view::Vec3 n = view::cross(dv, du);
  const float len = view::length(n);
  if (len < 1e-7f) return {0.0f, 0.0f, 1.0f};  // a pinch, or a hole closed to nothing
  return n * (1.0f / len);
}

/// A unit quaternion from an orthonormal, right-handed frame given as the images of the
/// local axes. The standard branch-on-the-largest-diagonal form: the naive trace formula
/// loses all its precision when the rotation is near a half-turn, which on a closed
/// surface is most of the far side.
std::array<float, 4> quatOf(const view::Vec3& ex, const view::Vec3& ey,
                            const view::Vec3& ez) {
  const float m00 = ex.x, m10 = ex.y, m20 = ex.z;
  const float m01 = ey.x, m11 = ey.y, m21 = ey.z;
  const float m02 = ez.x, m12 = ez.y, m22 = ez.z;
  const float trace = m00 + m11 + m22;
  if (trace > 0.0f) {
    const float s = std::sqrt(trace + 1.0f) * 2.0f;
    return {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
  }
  if (m00 > m11 && m00 > m22) {
    const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    return {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
  }
  if (m11 > m22) {
    const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    return {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
  }
  const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
  return {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
}

/// Every warp in the catalogue repeats after two laps of either lattice axis - one lap
/// for the orientable ones, two where a seam reverses a coordinate and the board has to
/// go round twice to come home. `period` is that distance in *cells* (two laps, `2 * nx`
/// or `2 * nz`); the wrap lives here, in cells, because only the board knows its own
/// extent (M17.9). Keeping the slide inside that window costs nothing and stops a long
/// drag grinding the trigonometry down to noise.
float wrapSlide(float s, float period) {
  return std::fmod(s, period);
}

}  // namespace

bool PlaySurface::slidesAlongRanks(const VariantSpec& v) noexcept {
  const app::SurfaceKind kind = app::overtureSignature(v).surface;
  return kind == app::SurfaceKind::Torus || kind == app::SurfaceKind::Klein;
}

PlaySurface PlaySurface::build(const VariantSpec& v, SurfacePose pose) {
  PlaySurface s;
  if (!hasPlaySurface(v)) return s;
  // How much of the ribbon's stretch a *board* can afford. At the library screen's full
  // stretch each cell is nine times longer than it is wide, which reads beautifully as a
  // ribbon and not at all as a board: you cannot see which square you are on. Half of it
  // is about as little as the shape will take - any less and the ribbon is wider than the
  // loop it goes round, and the band passes through itself - and it is the same kind of
  // stated legibility cheat as the torus's pulled-open hole.
  pose.stretch = 0.5f;
  const app::SurfaceKind kind = app::overtureSignature(v).surface;
  const int nx = static_cast<int>(v.dims.extent(0));
  const int nz = static_cast<int>(v.dims.extent(1));
  s.nx_ = nx;
  s.nz_ = nz;
  const float fnx = static_cast<float>(nx);
  const float fnz = static_cast<float>(nz);

  // The slide is in cells; the surface is parametrised over the whole board. Taken out of
  // the pose and folded into every sample, so nothing below this line knows about it.
  // Two laps is `2 * nx` cells along the files and `2 * nz` along the ranks, so a full
  // drag carries a1 all the way round and home rather than snapping after two squares.
  const float su = wrapSlide(pose.slideU, 2.0f * fnx) / fnx;
  const float sv = slidesAlongRanks(v) ? wrapSlide(pose.slideV, 2.0f * fnz) / fnz : 0.0f;
  pose.slideU = 0.0f;
  pose.slideV = 0.0f;
  const auto at = [&](float u, float vv) { return sample(kind, pose, u + su, vv + sv); };

  // The gapless grid first: every patch and seat is cut from it, the pick ray is tested
  // against it, and the shape's axis is its cross-section centroid. Sampling the surface
  // once, up front, is what keeps the three in step - ADR-0011's invariant.
  const int cu = nx * kSubdiv + 1;
  const int cv = nz * kSubdiv + 1;
  s.corners_.reserve(static_cast<std::size_t>(cu * cv));
  for (int i = 0; i < cu; ++i) {
    for (int j = 0; j < cv; ++j) {
      s.corners_.push_back(at(static_cast<float>(i) / static_cast<float>(nx * kSubdiv),
                              static_cast<float>(j) / static_cast<float>(nz * kSubdiv)));
    }
  }

  // The shape's axis at each rank: the centroid of the cross-section. "Outward" at a
  // point is the side away from it, and that is how the normal stays outward after the
  // eversion sweeps the ring radius through zero and reverses the parametrisation's
  // handedness. `cross(dv, du)` alone would leave every piece standing inside the
  // turned-out shape, hidden and unplayable (M17.7).
  std::vector<view::Vec3> axisAt(static_cast<std::size_t>(cv));
  for (int j = 0; j < cv; ++j) {
    view::Vec3 sum{0.0f, 0.0f, 0.0f};
    for (int i = 0; i < cu; ++i) {
      sum = sum + s.corners_[static_cast<std::size_t>(i * cv + j)];
    }
    axisAt[static_cast<std::size_t>(j)] = sum * (1.0f / static_cast<float>(cu));
  }
  const auto axisRef = [&](float vv) {
    const float t = vv * static_cast<float>(nz * kSubdiv);
    const int j0 = std::clamp(static_cast<int>(std::floor(t)), 0, cv - 1);
    const int j1 = std::min(j0 + 1, cv - 1);
    const float a = t - static_cast<float>(j0);
    return axisAt[static_cast<std::size_t>(j0)] * (1.0f - a) +
           axisAt[static_cast<std::size_t>(j1)] * a;
  };
  const auto nrm = [&](float u, float vv) {
    view::Vec3 n = normalAt(kind, pose, u + su, vv + sv, nx, nz);
    const view::Vec3 out = at(u, vv) - axisRef(vv);
    if (view::length(out) > 1e-5f && view::dot(n, out) < 0.0f) n = n * -1.0f;
    return n;
  };

  s.seats_.reserve(static_cast<std::size_t>(nx * nz));
  s.patches_.reserve(static_cast<std::size_t>(nx * nz));
  constexpr int kSide = kSubdiv + 1;
  for (int f = 0; f < nx; ++f) {
    for (int r = 0; r < nz; ++r) {
      const CellId cell = v.dims.toCell(Coord::of({f, r}));
      const float u = (static_cast<float>(f) + 0.5f) / fnx;
      const float vv = (static_cast<float>(r) + 0.5f) / fnz;

      // The square, as a patch of the surface. Its corners run across the cell's own
      // lattice footprint, inset so the line between squares is a real gap in the shape
      // rather than a stripe painted on it.
      SurfacePatch patch;
      patch.cell = cell;
      for (int i = 0; i < kSide; ++i) {
        for (int j = 0; j < kSide; ++j) {
          const float ou = kCoverage * (static_cast<float>(i) / kSubdiv - 0.5f) / fnx;
          const float ov = kCoverage * (static_cast<float>(j) / kSubdiv - 0.5f) / fnz;
          const std::size_t k = static_cast<std::size_t>(i * kSide + j);
          patch.pos[k] = at(u + ou, vv + ov);
          patch.normal[k] = nrm(u + ou, vv + ov);
        }
      }
      s.patches_.push_back(patch);

      SurfaceSeat seat;
      seat.cell = cell;
      seat.centre = at(u, vv);
      seat.normal = nrm(u, vv);
      // A piece is sized to the square it stands on, so the step is the distance to the
      // neighbouring seats rather than anything about the flat board.
      seat.stepU = 0.5f * (view::length(at(u + 1.0f / fnx, vv) - seat.centre) +
                           view::length(seat.centre - at(u - 1.0f / fnx, vv)));
      seat.stepV = 0.5f * (view::length(at(u, vv + 1.0f / fnz) - seat.centre) +
                           view::length(seat.centre - at(u, vv - 1.0f / fnz)));
      // Measured across the whole cell, not at a point: a piece should line up with the
      // row it stands in, and on a Klein bottle's crossing the tangent at the seat and
      // the direction of the next square are most of a right angle apart.
      const view::Vec3 along = at(u + 0.5f / fnx, vv) - at(u - 0.5f / fnx, vv);
      const view::Vec3 flat = along - seat.normal * view::dot(along, seat.normal);
      if (view::length(flat) > 1e-6f) {
        const view::Vec3 ex = view::normalize(flat);
        seat.quat = quatOf(ex, view::cross(seat.normal, ex), seat.normal);
      }
      s.seats_.push_back(seat);
    }
  }

  view::Bounds b;
  bool first = true;
  for (const view::Vec3& p : s.corners_) {
    if (first) {
      b = {p.x, p.y, p.z, p.x, p.y, p.z};
      first = false;
      continue;
    }
    b.minX = std::min(b.minX, p.x);
    b.minY = std::min(b.minY, p.y);
    b.minZ = std::min(b.minZ, p.z);
    b.maxX = std::max(b.maxX, p.x);
    b.maxY = std::max(b.maxY, p.y);
    b.maxZ = std::max(b.maxZ, p.z);
  }
  // Room for what stands on it: a piece is about a cell tall, and a camera framed on the
  // bare surface cuts the crowns off the pieces facing it.
  const float pad = 0.9f;
  b.minX -= pad;
  b.minY -= pad;
  b.minZ -= pad;
  b.maxX += pad;
  b.maxY += pad;
  b.maxZ += pad;
  s.bounds_ = b;
  return s;
}

CellId PlaySurface::pick(const view::OrbitCamera& camera, float width, float height,
                         float px, float py) const {
  if (corners_.empty()) return kInvalidCell;
  const view::OrbitCamera::Ray ray = camera.pickRay(px, py, width, height);
  const int cv = nz_ * kSubdiv + 1;
  const auto corner = [&](int i, int j) -> const view::Vec3& {
    return corners_[static_cast<std::size_t>(i * cv + j)];
  };

  // Moeller-Trumbore, nearest hit wins: a cell round the back of the shape is behind the
  // one in front of it, and the one in front is the answer.
  const auto hit = [&](const view::Vec3& a, const view::Vec3& b, const view::Vec3& c,
                       float& t) {
    const view::Vec3 e1 = b - a;
    const view::Vec3 e2 = c - a;
    const view::Vec3 pv = view::cross(ray.direction, e2);
    const float det = view::dot(e1, pv);
    if (std::abs(det) < 1e-9f) return false;
    const float inv = 1.0f / det;
    const view::Vec3 tv = ray.origin - a;
    const float uu = view::dot(tv, pv) * inv;
    if (uu < 0.0f || uu > 1.0f) return false;
    const view::Vec3 qv = view::cross(tv, e1);
    const float vv = view::dot(ray.direction, qv) * inv;
    if (vv < 0.0f || uu + vv > 1.0f) return false;
    t = view::dot(e2, qv) * inv;
    return t > 1e-4f;
  };

  CellId best = kInvalidCell;
  float bestT = 1e30f;
  for (int i = 0; i + 1 < nx_ * kSubdiv + 1; ++i) {
    for (int j = 0; j + 1 < cv; ++j) {
      const view::Vec3& a = corner(i, j);
      const view::Vec3& b = corner(i + 1, j);
      const view::Vec3& c = corner(i + 1, j + 1);
      const view::Vec3& d = corner(i, j + 1);
      float t = 0.0f;
      if ((hit(a, b, c, t) || hit(a, c, d, t)) && t < bestT) {
        bestT = t;
        best = seats_[static_cast<std::size_t>((i / kSubdiv) * nz_ + (j / kSubdiv))].cell;
      }
    }
  }
  return best;
}

}  // namespace cb::render
