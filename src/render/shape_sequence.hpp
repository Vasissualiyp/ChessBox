// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <tuple>
#include <vector>

#include "app/settings.hpp"
#include "app/shell.hpp"
#include "render/overture_scene.hpp"
#include "render/play_surface.hpp"
#include "variant/variant.hpp"
#include "view/camera.hpp"
#include "view/move_anim.hpp"

namespace cb::render {

/// The four-beat choreography for a followed move on a shape (M17.19). On a move the
/// board
/// **morphs** (its slide) until the player's own line to the start square is clear, the
/// camera flies to the piece, the piece travels with the camera chasing it, and the
/// camera flies home while the board morphs back. The piece is held still until the
/// camera has reached it, so nothing moves before you are looking at it. Pure beats in
/// `render::shapeBeat`; this holds only the clock and the values that must persist
/// between frames. Inactive means "no choreography": the old follow behaviour (a
/// capture's, too).
struct ShapeMoveSequence {
  bool active{false};
  float elapsed{0.0f};
  float travelSeconds{1.0f};
  CellId tokenFrom{kInvalidCell};
  CellId tokenTo{kInvalidCell};
  view::OrbitCamera startCam{};  ///< the player's own camera when the move began
  view::OrbitCamera camera{};    ///< the effective camera for the frame just computed
  SlideOffset startOffset{};     ///< the transient offset when the move began
  SlideOffset alignStart{};      ///< Align's target: clear from the start camera
  SlideOffset travelTo{};        ///< Travel's target: clear along the route
  SlideOffset offset{};          ///< the live transient offset, written to settings
  CellId trackedCell{kInvalidCell};
};

/// The choreography to hand a path that must not run it: an inactive sequence falls back
/// to the steady-state follow, which is what every capture that is not the M12.6 runner
/// wants.
inline const ShapeMoveSequence kNoShapeSequence{};

/// A memo of `PlaySurface::build` results keyed by (variant, pose), so the shape-follow
/// evaluator can rebuild the same prefix of poses for every requested frame of a move
/// without paying a full surface build at each fixed step (M12.6).
/// `simulateShapeSequence` replays a fixed-step prefix from zero on every call; without
/// this memo the cost would grow with the square of the elapsed time. References handed
/// out are owned by the entry, so they outlive the call that asked for them. The owner
/// clears it when the variant or the move changes.
class SurfaceCache {
 public:
  [[nodiscard]] std::shared_ptr<const PlaySurface> get(const VariantSpec& v,
                                                       const SurfacePose& pose);
  void clear() { entries_.clear(); }
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

 private:
  using Key = std::tuple<const VariantSpec*, float, float, float, float, float, float,
                         float, float>;
  std::vector<std::pair<Key, std::shared_ptr<const PlaySurface>>> entries_;
};

/// The surface pose the settings ask for right now: the player's own slide plus the
/// choreography's transient offset. Pure, so a still reproduces.
[[nodiscard]] SurfacePose surfacePose(const app::Settings& s, const VariantSpec& v);

/// Whether the shape-follow choreography is offered right now: the geometry view is on
/// over a glued variant, the camera follows the move, and the follow strength is not
/// zero. A capture states this through `--geometry`/`--follow`; the interactive loop
/// through its settings.
[[nodiscard]] bool shapeFollowApplies(const app::Shell& shell);

/// The lead-in and fly-back length of a followed move, in seconds, excluding the move's
/// own animation: Align + Approach + Return, divided by `shapeMorphSpeed` exactly as the
/// interactive sequence divides them. Used to budget a move's body in a played-back game
/// (M12.6).
[[nodiscard]] float shapeLeadSeconds(const app::Settings& st);

/// How far the chase camera sits from the followed piece: close enough to make the piece
/// the subject, at a fraction of the shape's span. The anti-clip search tests occlusion
/// at this same distance, so a seat it finds clear is clear for the camera that follows.
[[nodiscard]] float chaseEyeDistance(const PlaySurface& surf);

/// The chase camera's elevation as the tangent the shader/camera maths wants: the
/// settings slider is in degrees because that is what a player reads.
[[nodiscard]] float followLift(const app::Settings& s);

/// The seat the followed move is on right now, by nearest seat to the sampled position.
/// Aligning for the current cell keeps a long move clear of the shape the whole way
/// across, not only at the square it lands on. `kInvalidCell` when no shot is in flight.
[[nodiscard]] CellId currentFollowedCell(const app::Shell& shell,
                                         const PlaySurface& surf);

/// The slide offsets that keep the cell `followed` from clipping, for the current
/// settings and camera. Pure and deterministic, so a capture gets the same value the
/// interactive loop eases towards. `kInvalidCell` yields no offset.
[[nodiscard]] SlideOffset alignOffsetFor(const app::Shell& shell, CellId followed,
                                         SurfaceCache* cache = nullptr);

/// Build a fresh sequence for a move that is beginning: the camera it starts from, the
/// start/align/travel offsets, and the tokens. `from` is the camera actually drawn last
/// frame, so a move that starts while an earlier return is still running has no cut. Does
/// not touch the move's progress (the caller decides that).
[[nodiscard]] ShapeMoveSequence beginShapeSequence(app::Shell& shell,
                                                   const view::MovePath& path,
                                                   float travelSeconds,
                                                   const view::OrbitCamera& from,
                                                   SurfaceCache* cache = nullptr);

/// Advance the shape-follow choreography one frame (M17.19). It leaves the effective
/// camera in `seq.camera` and the transient slide in the settings. The piece's own clock
/// is driven here too: pinned at the start through the lead-in, running during Travel,
/// pinned at the end through the return. When `cache` is given, the surfaces it needs are
/// memoised by pose, so a repeated fixed-step prefix costs nothing above the first run.
void stepShapeSequenceOnce(app::Shell& shell, ShapeMoveSequence& seq, float dt,
                           SurfaceCache* cache = nullptr);

/// The choreography's state at `elapsed` seconds into a move that starts at `shell`'s
/// current position, as if it had been running since 0 - found by fixed-step simulation
/// from a fresh sequence, not recalled from any real frame's history. Two calls at the
/// same `elapsed` for the same move produce the same result, which is what lets a clip's
/// frames be requested out of order or more than once (M12.6).
[[nodiscard]] ShapeMoveSequence simulateShapeSequence(app::Shell& shell,
                                                      const view::MovePath& path,
                                                      float travelSeconds, float elapsed,
                                                      SurfaceCache* cache = nullptr);

/// The time in seconds at which a move's choreography has fully settled - the board home
/// and the camera back - found by fixed-step simulation. Used to budget a move's body in
/// a played-back game so a dwell holds a still picture rather than catching the board
/// mid-morph (M12.6). Never less than `shapeLeadSeconds` plus the travel.
[[nodiscard]] float shapeSequenceSettleSeconds(app::Shell& shell,
                                               const view::MovePath& path,
                                               float travelSeconds,
                                               SurfaceCache* cache = nullptr);

/// The fixed internal step the replay uses, in seconds. Independent of the caller's
/// requested elapsed or frame rate, so two calls at the same time agree bit for bit.
inline constexpr float kShapeSimStep = 1.0f / 120.0f;

}  // namespace cb::render
