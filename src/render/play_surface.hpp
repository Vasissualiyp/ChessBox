// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <vector>

#include "app/session.hpp"
#include "render/board_renderer.hpp"
#include "render/overture_scene.hpp"
#include "variant/variant.hpp"
#include "view/camera.hpp"
#include "view/layout.hpp"
#include "view/move_anim.hpp"
#include "view/move_camera.hpp"
#include "view/seams.hpp"

namespace cb::render {

class PlaySurface;

/// Frame `session`'s camera on whatever the geometry view would show right now: the play
/// surface's own bounds with `headroom = 0` (M17.11) while `options.surface` is set, the
/// flat layout's bounds otherwise. Reads nothing from `session` but its variant and
/// placements, so it is correct - and cheap - to call whenever anything that changes what
/// is drawn has changed: the surface toggle, the pose, or the variant itself. The caller
/// never has to work out which of the three actually happened (M17.14).
void frameGeometryCamera(app::Session& session, const BoardOptions& options,
                         SurfacePose pose);

/// Where the travelling piece sits on `surf` at progress `t`, and which way it stands. A
/// leap arcs outward along the blended normal; a glide walks the seats the engine's own
/// route passes through, in order, with no seam special case - on the surface a glued
/// edge is one continuous place, so there is no gap to open a doorway across. Pure in
/// `t`, like `moveCamera` and like an overture (M17.15).
struct SurfaceMoveSample {
  view::Vec3 position{};
  view::Vec3 normal{0.0f, 0.0f, 1.0f};
  std::array<float, 4> quat{{0.0f, 0.0f, 0.0f, 1.0f}};
  float fit{1.0f};  ///< the piece scale factor seats already carry
};
[[nodiscard]] SurfaceMoveSample surfaceMoveSample(const view::MovePath& path,
                                                  const PlaySurface& surf, float t);

/// A slide offset, in cells, on the surface's two axes: the same two numbers the
/// middle-mouse drag sets, and the two `PlaySurface::build` samples the pose with.
struct SlideOffset {
  float u{0.0f};
  float v{0.0f};
};

/// The stages a followed move's camera passes through on a shape (M17.19). The move is
/// bracketed: first the board **morphs** (the slide) until the camera's own line to the
/// start square is clear, then the camera flies to the piece, then the piece travels with
/// the camera chasing it, and finally the camera flies back while the board morphs home.
/// The piece is held still through `Align`/`Approach` so it does not start moving before
/// the camera has reached it.
enum class ShapeStage : std::uint8_t { Align, Approach, Travel, Return, Done };

/// Where a shape-followed move is in its camera choreography, and how far through the
/// stage. Pure in `elapsed`, so a capture that states a time reproduces it.
struct ShapeBeat {
  ShapeStage stage{ShapeStage::Done};
  float local{1.0f};  ///< 0..1 within the stage (1 once done)
};

/// Map elapsed sequence seconds to a stage and its local progress. Lead-in is
/// `alignSeconds` then `approachSeconds`; the middle is the move's own `travelSeconds`;
/// `returnSeconds` is the fly-back. A zero-length stage is skipped (its boundary falls
/// through to the next). Pure.
[[nodiscard]] ShapeBeat shapeBeat(float elapsed, float alignSeconds,
                                  float approachSeconds, float travelSeconds,
                                  float returnSeconds) noexcept;

/// Smoothstep on `local` in [0,1], the ease every stage uses so nothing starts or stops
/// with a jerk.
[[nodiscard]] float shapeEase(float local) noexcept;

/// The chase camera's elevation above the followed piece's tangent plane, expressed as a
/// tangent (the ratio of the eye's normal lift to its distance behind), for a caller with
/// no setting: 30 degrees. `Settings::followElevationDeg` overrides it.
inline constexpr float kDefaultFollowLift = 0.57735027f;

/// The chase camera's eye direction from the piece: behind the travel direction, lifted
/// toward the surface normal's component perpendicular to that travel, by `lift` (a
/// tangent). Shared by `surfaceChaseCamera` and the anti-clip search (`alignSlideU`) so
/// the search can never test a different camera than the one drawn (M17.20).
[[nodiscard]] view::Vec3 chaseEyeDirection(const view::Vec3& normal,
                                           const view::Vec3& travel, float lift);

/// The largest distance along `direction` from `target` (toward the eye), up to
/// `maxDistance`, at which the eye is not blocked from `target` by `surf` itself - found
/// by bisection against `PlaySurface::blocked`. Never returns less than `minDistance`, so
/// a degenerate position still has a defined place to put the camera rather than one that
/// is found by trusting a fixed distance that happens to reach past the shape (M17.20).
[[nodiscard]] float clearEyeDistance(const PlaySurface& surf, const view::Vec3& target,
                                     const view::Vec3& direction, float maxDistance,
                                     float minDistance = 0.5f);

/// The slide offsets that best keep `target`'s cell visible to a camera following a move
/// along `travel`: they maximise the cell's `facing` (its outward normal toward the
/// camera) and prefer offsets where nothing else on the shape lies between the two.
/// `eyeDistance` is how far the camera will actually sit from the cell - the search must
/// test occlusion at *that* distance, or a close follow can still be behind the tube.
/// This is the anti-clip search (M17.17, revised): for a closed ring (`torus`, `klein`)
/// only - an open tube or ribbon has no inner/outer side to turn, so it returns zero.
[[nodiscard]] SlideOffset alignSlideU(const VariantSpec& v, CellId target,
                                      const view::Vec3& travel, float eyeDistance,
                                      float lift = kDefaultFollowLift);

/// The chase anti-clip for a followed move at progress `t`: like the overload above, but
/// the eye direction is scored against each candidate pose's **own** travel, sampled from
/// `surfaceMoveSample` on the candidate. The camera's travel depends on the pose the
/// board is turned to (a chase's direction is the piece's motion on the drawn shape), so
/// a single travel read off the starting pose asks a different question than the camera
/// answers - which is how a rotation the search called clear still put the camera through
/// the tube (M17.20). This is the form the front end uses. With `hint`, the previous
/// frame's offset is scored first and accepted outright when it is still basically clear,
/// so a piece's cell-to-cell motion does not re-target a different rotation purely
/// because the global argmax ticked - the hysteresis that stops the shape jittering
/// (M17.20).
[[nodiscard]] SlideOffset alignSlideU(const VariantSpec& v, CellId target,
                                      const view::MovePath& path, float t,
                                      float eyeDistance, float lift = kDefaultFollowLift,
                                      const SlideOffset* hint = nullptr);

/// A chase camera for a followed move on the shape (M17.16, revised): the piece centred
/// and the camera behind it along `travel`. With `upright` the frame is rolled so the
/// piece's own up (its surface normal) is the view's up - the piece stands vertically in
/// the middle of the view. Without it the frame is not rolled, so the piece tilts and can
/// flip with the shape. One camera for one continuous move; the route is
/// `surfaceMoveSample`'s, never cut at a seam.
[[nodiscard]] view::OrbitCamera surfaceChaseCamera(const SurfaceMoveSample& piece,
                                                   const view::Vec3& travel,
                                                   float distance, bool upright,
                                                   float lift = kDefaultFollowLift);

/// The chase camera for a move on the shape at progress `t` (M17.16 revision). This is
/// the form the front end uses: it samples the piece and its travel from
/// `surfaceMoveSample` itself, estimating the travel over a window in `t` rather than
/// between two consecutive cells, so the camera is a **continuous** function of `t` - no
/// swing at a cell boundary - and a leap follows its chord rather than the arc that
/// reverses under the piece. Pure in `t`, so a capture reproduces and reverse playback is
/// a falling `t`.
[[nodiscard]] view::OrbitCamera surfaceFollowCamera(const view::MovePath& path,
                                                    const PlaySurface& surf, float t,
                                                    float distance, bool upright,
                                                    float lift = kDefaultFollowLift);

/// True when the chase camera's own path over the next `lookahead` of the move - the
/// segment its eye moves along, or its line to the piece at the end of that step - would
/// cross the board. This is the condition the morph exists to clear (M17.19): the front
/// end looks ahead, and turns the board only while this is true. Pure in `t`.
[[nodiscard]] bool followClips(const view::MovePath& path, const PlaySurface& surf,
                               float t, float lookahead, float distance,
                               float lift = kDefaultFollowLift);

/// The slide offsets that turn `target`'s cell toward a camera looking along `toCamera`
/// (unit, the direction from the cell toward the eye), preferring offsets where nothing
/// on the shape lies between the two, tested at `eyeDistance`. This is the turntable
/// anti-clip (M17.16/17): the **shape** rotates so the followed piece comes round to the
/// near side, while the camera keeps the angle the player set.
[[nodiscard]] SlideOffset alignSlideToFace(const VariantSpec& v, CellId target,
                                           const view::Vec3& toCamera, float eyeDistance);

/// How many corners a square is cut into per axis. A square on a curved board is not a
/// square: it is a patch of the surface, and this is how finely that patch is sampled.
inline constexpr int kSurfaceSubdiv = 4;
inline constexpr int kSurfaceCorners = (kSurfaceSubdiv + 1) * (kSurfaceSubdiv + 1);

/// One square, as the piece of surface it actually is.
///
/// Not a rectangle turned to face the right way - a grid of corners lying *on* the
/// surface, which is the whole difference. A rectangle has to be oriented, and it can
/// only be tangent to the surface at one point, so on anything that curves fast the
/// squares overlap at one end and gape at the other and the board reads as fish scales.
/// A patch has no orientation to get wrong: its corners are the surface's own points, and
/// two squares either side of a lattice edge are cut from the same curve.
///
/// `pos[i * (kSurfaceSubdiv + 1) + j]` is the corner `i` steps along the files and `j`
/// along the ranks. `normal` is the surface normal there, so shading is smooth across the
/// patch and continuous into the next one.
struct SurfacePatch {
  CellId cell{kInvalidCell};
  std::array<view::Vec3, kSurfaceCorners> pos{};
  std::array<view::Vec3, kSurfaceCorners> normal{};
};

/// Where a piece stands on a square, and which way is up there.
///
/// A piece *is* an object with an orientation - unlike a square, which is a piece of the
/// board - so it keeps a frame: local +X along the files, +Y across the ranks, +Z out of
/// the surface, right-handed. `stepU`/`stepV` are the world size of one lattice cell
/// there, which is what a piece is scaled to fit: an embedding stretches the board where
/// it opens a hole and squeezes it where it closes one.
struct SurfaceSeat {
  CellId cell{kInvalidCell};
  /// The seat's lattice coordinate, in cells - needed to find the *surface* point on the
  /// boundary between two seats (the edge of an orthogonal step, the corner of a diagonal
  /// one), rather than the chord midpoint of their centres, which on a fast-curving board
  /// (a torus's inner ring) sits well inside the surface (M17.19).
  int file{0};
  int rank{0};
  view::Vec3 centre{};
  view::Vec3 normal{};
  std::array<float, 4> quat{{0.0f, 0.0f, 0.0f, 1.0f}};
  float stepU{1.0f};
  float stepV{1.0f};
};

/// A seam rail on the shape: the tint to write into a patch's vertex colours at the wrap
/// edge, and how strongly to blend it there. The flat view draws a coloured rim where a
/// board is glued to itself, but a torus has no edge to draw it on - the wrap locus is
/// the ring of cells where the lattice coordinate goes from its last value back to its
/// first, and the shape tints that ring instead. Empty `weight` means the corner is not
/// on a rail (M17.21).
struct SurfaceRail {
  view::Rgba color{};
  /// 1 on the wrap edge, 0 on every other corner. The blend across the one subdivision
  /// step between them is the rasteriser's linear interpolation, so the rail fades to the
  /// board rather than ending on a hard line.
  float weight{0.0f};
};

/// The rail on one patch corner of `cell` for one lattice axis: `axis` is 0 for the
/// files, 1 for the ranks, and `i`/`j` are the corner's subdivision indices, `i == 0` the
/// low-file edge and `j == 0` the low-rank edge. A rail exists only where the cell is on
/// that axis's wrap ring *and* the flat view's `SeamMap` finds a glued seam there, and it
/// takes that seam's own colour - so the two views can never disagree about where the
/// seam is, or what colour it is. A mirror, or a cell not on the ring, gets nothing
/// (M17.21).
[[nodiscard]] SurfaceRail surfaceRail(const view::SeamMap& seams, CellId cell, int axis,
                                      int i, int j);

/// The play board as the shape its geometry describes (M17).
///
/// One object answers the three questions the geometry view asks: what shape is each
/// square, where does a piece stand on it, and which square is under the cursor. They
/// share one sampling of the surface on purpose - ADR-0011's invariant is that the pick
/// ray follows what was drawn, and the cheapest way to guarantee that is to give the
/// picker and the mesh builder the same points.
///
/// Empty for anything without a surface to become: `standard`, a mirror box, and every
/// board of three dimensions or more, where the lattice already *is* the shape.
class PlaySurface {
 public:
  static constexpr int kSubdiv = kSurfaceSubdiv;

  /// The fraction of its lattice cell a square covers, leaving the line between squares
  /// that is how a board reads as a board.
  static constexpr float kCoverage = 0.90f;

  /// How thick a square is, in world units - the same slab the flat board's cells are.
  static constexpr float kThickness = 0.11f;

  /// The surface a variant plays on, in the pose given. The board fixes the ribbon's
  /// stretch itself: how legible a square is is not something a front end should be able
  /// to get wrong.
  [[nodiscard]] static PlaySurface build(const VariantSpec& v, SurfacePose pose = {});

  /// Whether sliding the board along its ranks means anything here. It does exactly when
  /// the ranks are glued: on a cylinder or a ribbon the rank edges are free, so sliding
  /// along them would translate the whole shape through space and change nothing.
  [[nodiscard]] static bool slidesAlongRanks(const VariantSpec& v) noexcept;

  [[nodiscard]] bool empty() const noexcept { return seats_.empty(); }
  /// True for the authored D >= 3 shapes (`torus3d`, `hyper4`): a stack of flat tiles
  /// with no continuous parametrisation to key a wrap ring off, so the seam rails and the
  /// ring labels are offered for the parametrised 2-D surfaces only (M17.21).
  [[nodiscard]] bool stacked() const noexcept { return stacked_; }
  /// One per cell: where a piece stands and which way it stands.
  [[nodiscard]] const std::vector<SurfaceSeat>& seats() const noexcept { return seats_; }
  /// One per cell: the patch of surface the square is.
  [[nodiscard]] const std::vector<SurfacePatch>& patches() const noexcept {
    return patches_;
  }
  /// What the shape occupies, for a camera that has to frame it. The flat layout's
  /// bounds are the wrong answer: the surface is a different size and sits somewhere
  /// else entirely, which is why the board first arrived off in a corner.
  [[nodiscard]] view::Bounds bounds() const noexcept { return bounds_; }

  /// The surface point at a lattice coordinate, in *cells* (fractional allowed), in the
  /// pose the board was built with. On a parametrised two-dimensional shape this is the
  /// genuine surface point - so the boundary between two squares can be found on the
  /// surface rather than as the chord midpoint of their centres (M17.19). A stacked
  /// (`D >= 3`) shape has no such parametrisation and returns `false`.
  [[nodiscard]] bool pointAt(float file, float rank, view::Vec3& out) const;

  /// The surface point on the boundary between two seats - the shared edge of an
  /// orthogonal step or the shared corner of a diagonal one - wrapping a glued axis the
  /// short way so a seam's edge is on the correct side. `false` for a stacked shape (no
  /// parametrisation).
  [[nodiscard]] bool nearestBoundary(const SurfaceSeat& a, const SurfaceSeat& b,
                                     view::Vec3& out) const;

  /// The cell under a screen pixel, or `kInvalidCell`. Ray-tests the surface itself -
  /// the same points the squares were cut from, with no gaps between them - so a click
  /// resolves to the cell the cursor is over and never falls between two squares.
  [[nodiscard]] CellId pick(const view::OrbitCamera& camera, float width, float height,
                            float px, float py) const;

  /// True when any drawn tile lies between `eye` and `target` (nearer than it by more
  /// than `eps`) - the anti-clip test that keeps the shape from coming between a followed
  /// piece and the camera (M17.16 revision).
  [[nodiscard]] bool blocked(const view::Vec3& eye, const view::Vec3& target,
                             float eps) const;

 private:
  /// The three- and four-dimensional shapes (M17.12): one flat tile per cell from
  /// `playShapePosition`, since above two dimensions the "surface" is a stack of sheets
  /// rather than one parametrised sheet.
  void buildStacked(const VariantSpec& v);

  std::vector<SurfaceSeat> seats_;
  std::vector<SurfacePatch> patches_;
  /// A gapless grid of surface points, `nx * kSubdiv + 1` by `nz * kSubdiv + 1`, that
  /// the pick ray is tested against.
  std::vector<view::Vec3> corners_;
  /// The D >= 3 shapes' tiles: one quad per cell, the surface the pick ray is tested
  /// against. Empty on a two-dimensional board, which uses `corners_`.
  bool stacked_{false};
  std::vector<std::array<view::Vec3, 4>> quads_;
  std::vector<CellId> quadCells_;
  int nx_{0};
  int nz_{0};
  /// What `pointAt` needs to sample the parametrised surface: the warp, the folded pose
  /// (stretch applied, slide zeroed), and the slide itself in normalised units.
  app::SurfaceKind kind_{};
  SurfacePose surfacePose_{};
  float su_{0.0f};
  float sv_{0.0f};
  view::Bounds bounds_{};
};

}  // namespace cb::render
