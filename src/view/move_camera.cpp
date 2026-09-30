// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/move_camera.hpp"

#include <algorithm>
#include <cmath>

namespace cb::view {
namespace {

float applyEase(Ease e, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (e) {
    case Ease::Linear:
      return t;
    case Ease::Smooth:
      return t * t * (3.0f - 2.0f * t);
    case Ease::In:
      return t * t;
    case Ease::Out:
      return 1.0f - (1.0f - t) * (1.0f - t);
  }
  return t;
}

Vec3 mix(const Vec3& a, const Vec3& b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

/// A lattice direction read as a world vector, through the same screen-axis mapping the
/// animation uses (`MoveAnimation::start`). A direction with no screen-axis support -
/// a move along a grid axis - projects to nothing and the caller falls back to the run's
/// world delta.
Vec3 worldDir(const ViewConfig& cfg, const Direction& d) {
  float out[3]{0.0f, 0.0f, 0.0f};
  for (std::size_t i = 0; i < cfg.screenAxes.size() && i < 3; ++i) {
    const float scale = i == 2 ? cfg.depthSpacing : 1.0f;
    out[i] = static_cast<float>(d.v[cfg.screenAxes[i]]) * scale;
  }
  const float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
  if (len > 1e-6f) {
    out[0] /= len;
    out[1] /= len;
    out[2] /= len;
  }
  return {out[0], out[1], out[2]};
}

/// A point at normalised arc length `s` along a run's polyline.
Vec3 pointAlong(const RouteRun& run, float s) {
  if (run.points.empty()) return {};
  if (run.points.size() == 1) return run.points.front();
  s = std::clamp(s, 0.0f, 1.0f);

  std::vector<float> cum(run.points.size(), 0.0f);
  float total = 0.0f;
  for (std::size_t i = 1; i < run.points.size(); ++i) {
    total += length(run.points[i] - run.points[i - 1]);
    cum[i] = total;
  }
  if (total <= 1e-5f) return run.points.back();

  const float want = s * total;
  std::size_t i = 1;
  while (i + 1 < cum.size() && cum[i] < want) ++i;
  const float seg = std::max(1e-5f, cum[i] - cum[i - 1]);
  return mix(run.points[i - 1], run.points[i], (want - cum[i - 1]) / seg);
}

}  // namespace

std::vector<RouteRun> routeRuns(const MovePath& path,
                                const std::vector<Placement>& placements) {
  std::vector<RouteRun> runs;
  if (path.from == kInvalidCell || path.to == kInvalidCell) return runs;

  // The handful of cells the route touches, in order. A linear scan rather than a
  // cell-indexed table: the lattice can hold millions of cells and a move touches a
  // handful, the same trade `MoveAnimation::start` already makes.
  std::vector<CellId> wanted{path.from};
  for (const PathStep& s : path.steps) wanted.push_back(s.to);
  std::vector<Vec3> at(wanted.size());
  std::vector<bool> got(wanted.size(), false);
  for (const Placement& p : placements) {
    for (std::size_t i = 0; i < wanted.size(); ++i) {
      if (!got[i] && p.cell == wanted[i]) {
        at[i] = {p.x, p.y, p.z};
        got[i] = true;
      }
    }
  }
  for (bool g : got) {
    if (!g) return {};  // a cell the view does not draw; nothing to follow
  }

  RouteRun run;
  run.dir = path.startDir;
  run.points.push_back(at[0]);
  for (std::size_t i = 0; i < path.steps.size(); ++i) {
    const PathStep& s = path.steps[i];
    if (s.kind == StepKind::Interior) {
      run.points.push_back(at[i + 1]);
      run.dir = s.dir;  // the direction this run is actually travelling
      continue;
    }
    // A seam or a wall ends the run *at the cell it was entered from* - the far side of a
    // portal is not part of this run, or the camera would follow a chord straight across
    // the board. The next run begins on the far side, already travelling in the
    // post-transport direction the step recorded.
    run.endedWith = s.kind;
    run.faceAxis = s.faceAxis;
    run.faceSide = s.faceSide;
    runs.push_back(std::move(run));
    run = RouteRun{};
    run.dir = s.dir;
    run.points.push_back(at[i + 1]);
  }
  runs.push_back(std::move(run));

  const float n = static_cast<float>(runs.size());
  for (std::size_t r = 0; r < runs.size(); ++r) {
    runs[r].t0 = static_cast<float>(r) / n;
    runs[r].t1 = static_cast<float>(r + 1) / n;
  }
  return runs;
}

CameraPose moveCamera(const MovePath& path, const std::vector<Placement>& placements,
                      const ViewConfig& cfg, const Bounds& scene,
                      const CameraPolicy& policy, float t) {
  const OrbitCamera settled = OrbitCamera::frame(scene);
  const CameraPose base{settled.target, settled.distance, settled.yaw, settled.pitch,
                        0.0f};
  if (policy.follow == FollowMode::Off) return base;

  const std::vector<RouteRun> runs = routeRuns(path, placements);
  if (runs.empty()) return base;

  t = std::clamp(t, 0.0f, 1.0f);
  const RouteRun* run = &runs.back();
  for (const RouteRun& r : runs) {
    if (t <= r.t1 + 1e-6f) {
      run = &r;
      break;
    }
  }

  const float span = std::max(1e-5f, run->t1 - run->t0);
  const float local = applyEase(Ease::Smooth, (t - run->t0) / span);
  CameraPose pose = base;
  pose.target = pointAlong(*run, local);

  const Vec3 first = run->points.front();
  const Vec3 last = run->points.back();
  const float travel = length(last - first);
  // Orient by the run's **transported** direction, not by the delta between its two world
  // ends: on a glued board that delta can be a long chord across the board while the
  // piece actually travelled a short way, and on a non-orientable seam the transport
  // flips a component the chord would hide. Fall back to the chord only for a direction
  // with no screen-axis support (a grid-axis move, whose camera story is M11.4).
  Vec3 dir = worldDir(cfg, run->dir);
  if (length(dir) < 1e-4f && travel > 1e-4f) dir = normalize(last - first);
  if (length(dir) > 1e-4f) {
    // Lead the piece along the run, so a fast slide is seen from where it is going
    // rather than where it has been.
    pose.target = pose.target + dir * policy.lead;
    // Look along the travel. The eye sits behind the piece, so yaw points opposite the
    // travel in the board plane (the eye offset is (sin yaw, -cos yaw, sin pitch)); any
    // depth component raises the pitch.
    pose.yaw = std::atan2(-dir.x, dir.y);
    const float flat = std::hypot(dir.x, dir.y);
    pose.pitch = std::clamp(0.9f + std::atan2(dir.z, flat) * 0.6f, 0.2f, 1.45f);
    // Pull back with travel so a long move is framed whole; never closer than the floor.
    pose.distance = std::max(policy.minDistance, pose.distance + policy.pull * travel);
  }
  return pose;
}

OrbitCamera toOrbit(const CameraPose& pose) {
  OrbitCamera cam;
  cam.target = pose.target;
  cam.distance = pose.distance;
  cam.yaw = pose.yaw;
  cam.pitch = pose.pitch;
  return cam;
}

}  // namespace cb::view
