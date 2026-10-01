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

/// The slide offset, in cells, that turns `target`'s cell most toward `camera`: the
/// facing search behind ALIGN (M17.17). For a closed ring (`torus`, `klein`) only - an
/// open tube or ribbon has no inner/outer side to turn, so it returns 0. A search rather
/// than a closed form: the embeddings have no general inverse for "which slide makes this
/// point face outward", and a 16-point sweep is cheap.
[[nodiscard]] float alignSlideU(const VariantSpec& v, CellId target,
                                const view::OrbitCamera& camera);

/// A chase camera for a followed move on the shape (M17.16, revised): the piece centred,
/// the camera behind it along `travel`, and the frame rolled so the piece's own up (its
/// surface normal) is the view's up - so the piece stands vertically in the middle of the
/// view, the way a third-person follow would. One camera for one continuous move; the
/// route is `surfaceMoveSample`'s, never cut at a seam.
[[nodiscard]] view::OrbitCamera surfaceChaseCamera(const SurfaceMoveSample& piece,
                                                   const view::Vec3& travel,
                                                   float distance);

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
  view::Vec3 centre{};
  view::Vec3 normal{};
  std::array<float, 4> quat{{0.0f, 0.0f, 0.0f, 1.0f}};
  float stepU{1.0f};
  float stepV{1.0f};
};

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

  /// The cell under a screen pixel, or `kInvalidCell`. Ray-tests the surface itself -
  /// the same points the squares were cut from, with no gaps between them - so a click
  /// resolves to the cell the cursor is over and never falls between two squares.
  [[nodiscard]] CellId pick(const view::OrbitCamera& camera, float width, float height,
                            float px, float py) const;

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
  view::Bounds bounds_{};
};

}  // namespace cb::render
