// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
//
// The move camera: the view that follows a piece through whatever the board does.
//
// It reads only data the engine has already generalised - `MovePath` (topology as tags),
// `Placement` (dimension as world positions) - and never decodes a `Coord` or asks a
// dimension count. That is the whole reason it works in 2-D, 6-D, on a Klein bottle and
// through a mirror with no `if (dims == ...)` anywhere: see M11.
#include <cstdint>
#include <vector>

#include "view/camera.hpp"
#include "view/layout.hpp"
#include "view/move_anim.hpp"

namespace cb::view {

/// A camera at one instant. Deliberately the same fields `OrbitCamera` already has, so a
/// pose is applied by assignment and there is no second notion of "a camera".
struct CameraPose {
  Vec3 target{};
  float distance{10.0f};
  float yaw{0.6f};
  float pitch{0.9f};
  /// 0 = follow the board's up; nonzero rolls the view for a loop or a rolled axis.
  /// Not yet consumed by `OrbitCamera` (which has no roll), so it is recorded here for
  /// the shot planner and ignored by `toOrbit`.
  float roll{0.0f};
};

/// How a shot interpolates between its two poses.
enum class Ease : std::uint8_t { Linear, Smooth, In, Out };

/// What a shot does, so the renderer can decide to cut rather than pan.
enum class ShotKind : std::uint8_t {
  Track,   ///< a continuous move of the camera
  Frame,   ///< hold a fixed framing
  Cut,     ///< a hard change of framing, no interpolation
  Portal,  ///< a portal crossing: the camera eases across the seam
};

/// One leg of a camera sequence, in normalised move time.
struct CameraShot {
  float t0{0.0f};
  float t1{1.0f};
  CameraPose a{};
  CameraPose b{};
  Ease ease{Ease::Smooth};
  ShotKind kind{ShotKind::Track};
};

/// What the camera is allowed to do. Keyed by geometry kind and view style, never by
/// dimension; every field is a policy, not a rule.
enum class FollowMode : std::uint8_t { Off, Piece, Route, Both };
enum class PortalPolicy : std::uint8_t { Cut, Fade, Orbit };
enum class BouncePolicy : std::uint8_t { Hold, Recoil };
enum class LeapPolicy : std::uint8_t { Follow, Frame };
enum class GridAxisPolicy : std::uint8_t { Pan, Cut };

struct CameraPolicy {
  FollowMode follow{FollowMode::Route};
  float lead{0.28f};        ///< how far ahead of the piece the target sits, in cells
  float pull{0.55f};        ///< extra distance as travel rises, in cells
  float minDistance{3.0f};  ///< never dolly closer than this
  float deadline{0.55f};    ///< seconds a cut may take; longer moves become shots
  PortalPolicy portal{PortalPolicy::Fade};
  BouncePolicy bounce{BouncePolicy::Hold};
  LeapPolicy leap{LeapPolicy::Frame};
  GridAxisPolicy grid{GridAxisPolicy::Cut};
};

/// One straight run of a move in world space: consecutive interior steps with no seam
/// between them. A portal or a bounce ends a run and the next begins on the far side.
struct RouteRun {
  std::vector<Vec3> points;
  float t0{0.0f};  ///< normalised span within the move, by run index
  float t1{1.0f};
  StepKind endedWith{StepKind::Interior};
  std::uint8_t faceAxis{0};
  Side faceSide{Side::Max};
  /// The direction the run travels, **after** any transport - the last step's
  /// `PathStep::dir`. On a non-orientable seam this is not the direction the piece set
  /// off in, which is exactly why the camera reads it instead of guessing from the
  /// points.
  Direction dir{};
};

/// Decompose a traced route into its straight runs. The animation and the camera share
/// this, so "what a run is" has exactly one definition. Returns empty when a cell on the
/// route is not in `placements` - a view that does not draw it has nothing to follow.
[[nodiscard]] std::vector<RouteRun> routeRuns(const MovePath& path,
                                              const std::vector<Placement>& placements);

/// The pose of the move camera at progress `t` in [0,1].
///
/// Pure: it reads no clock and no previous frame, so a `--shot` at a pinned `t` is
/// reproducible and reverse playback is just a falling `t` (the overture rule).
///
/// With `policy.follow == Off` it returns the settled board framing, which is a strict
/// no-op for the existing camera - every pre-M11 golden still holds. Otherwise the target
/// leads the moving piece by `policy.lead`, the camera looks along the run's travel and
/// pulls back with it, bounded by `policy.minDistance`.
[[nodiscard]] CameraPose moveCamera(const MovePath& path,
                                    const std::vector<Placement>& placements,
                                    const ViewConfig& cfg, const Bounds& scene,
                                    const CameraPolicy& policy, float t);

/// A point at normalised arc length `s` in [0,1] along a polyline. Shared by the move
/// camera and the geometry view's piece sampler, so the camera and the piece cannot
/// disagree about where the move currently is - exactly the class of bug ADR-0011's
/// invariant exists to rule out (M17.15).
[[nodiscard]] Vec3 pointAlong(const std::vector<Vec3>& points, float s);

/// Apply a pose to the orbit camera, by assignment. `roll` has no `OrbitCamera` field and
/// is dropped here; the flat board and the existing renderer never set it.
[[nodiscard]] OrbitCamera toOrbit(const CameraPose& pose);

}  // namespace cb::view
