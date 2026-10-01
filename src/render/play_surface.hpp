// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <vector>

#include "render/overture_scene.hpp"
#include "variant/variant.hpp"
#include "view/camera.hpp"
#include "view/layout.hpp"

namespace cb::render {

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
  std::vector<SurfaceSeat> seats_;
  std::vector<SurfacePatch> patches_;
  /// A gapless grid of surface points, `nx * kSubdiv + 1` by `nz * kSubdiv + 1`, that
  /// the pick ray is tested against.
  std::vector<view::Vec3> corners_;
  int nx_{0};
  int nz_{0};
  view::Bounds bounds_{};
};

}  // namespace cb::render
