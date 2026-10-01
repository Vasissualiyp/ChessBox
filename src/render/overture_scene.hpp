// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/overture.hpp"
#include "render/piece_icon.hpp"
#include "space/dims.hpp"
#include "view/theme.hpp"

namespace cb::render {

/// A point in the overture's own world. Y is up, the flat board lies in y = 0, files
/// run along x and ranks along z - the same handedness the board renderer uses, so a
/// pose that looks right here looks right there.
struct OvVec3 {
  float x{0}, y{0}, z{0};
};

/// What a quad is made of. A tone rather than a colour, so every palette decision stays
/// in `drawOverture` and a retheme cannot be defeated by a scene function that thought
/// it knew better.
enum class OvTone : std::uint8_t {
  Light,   ///< a light square
  Dark,    ///< a dark square
  Lit,     ///< called out by a rule - the accent
  Scorch,  ///< what a detonation left
  Mirror,  ///< a reflecting wall: bright metal, deliberately unhued
  Scrim,   ///< a dimming wash over a cell a rule has taken out of play
};

struct OvQuad {
  OvVec3 p[4];
  OvTone tone{OvTone::Light};
  float fade{1.0f};
  /// An explicit colour, for a face that is a portal rather than a board cell. A tone
  /// cannot name one: the seam ramp is computed from the theme, not stored in it. When
  /// set, `tone` is ignored and the face is drawn flat in this colour.
  bool hasColour{false};
  view::Rgba colour{};
};

/// A piece, standing along its surface's normal - which is what lets one stand on the
/// outside of a torus and another on the underside without either lying down.
struct OvToken {
  OvVec3 at;
  OvVec3 normal{0.0f, 1.0f, 0.0f};
  char glyph{'P'};
  bool white{true};
  float height{1.0f};
  float fade{1.0f};
  /// Drawn mirrored, for the bishop that comes home off a Moebius band.
  bool mirrored{false};
};

struct OvTrail {
  std::vector<OvVec3> pts;
  view::Rgba colour{};
  float width{2.0f};
  float fade{1.0f};
  bool dashed{false};
};

struct OvBurst {
  OvVec3 at;
  float radius{1.0f};
  float fade{1.0f};
  view::Rgba colour{};
  /// A cross rather than a ring: a refusal, not an event.
  bool cross{false};
};

/// Where the overture is seen from. `reach` is the world radius the pane must hold, so
/// growing it is how an overture zooms out as its object gets bigger - and because it is
/// a function of t like everything else, the zoom cannot drift out of step with the
/// shape.
struct OvCamera {
  float yaw{0.0f};
  float elev{1.2f};
  float reach{9.0f};
  float persp{0.10f};
};

/// One frame of an overture: everything to draw, and nothing about how.
struct OvertureScene {
  std::vector<OvQuad> quads;
  std::vector<OvToken> tokens;
  std::vector<OvTrail> trails;
  std::vector<OvBurst> bursts;
  OvCamera cam;
  std::string caption;
};

/// The scene, as a pure function of progress.
///
/// `t` runs 0 at the flat 8x8 to 1 at the formed shape. It reads no clock and no
/// previous frame, which is what makes reverse playback free - it is this function
/// called with a falling `t` - and a screenshot reproducible.
///
/// `intro` is honoured only by `Overture::Standard`, where it builds the board out of
/// nothing; see `app::OverturePlayer::intro`.
[[nodiscard]] OvertureScene overtureScene(app::Overture which, float t, bool intro,
                                          const view::Theme& theme);

/// The scene for a variant with no hand-authored overture, derived from its spec (M13).
///
/// Pure in `t`, like its sibling: it opens on the flat board, forms the surface the
/// variant's identifications imply, and unwinds. A data-only variant animates without a
/// line of scene code written for it.
[[nodiscard]] OvertureScene derivedOvertureScene(const VariantSpec& v, float t,
                                                 const view::Theme& theme);

/// How far a surface has been turned through itself, and nothing else about it (M17).
///
/// A shape is rigid; a surface is not. `evert` runs 0 to 1 and pulls the embedding
/// through its own middle: a torus's hole closes and reopens with what was inside it now
/// outside, a Moebius band is drawn through its own loop, and a cylinder - which no
/// rotation can turn inside out, being open at both ends - is rolled back over itself
/// the way a sock is. At 0 the surface is exactly the one the library screen draws, so
/// every existing capture still holds; the whole family is a pure function of the
/// number, like an overture, which is what makes a drag reversible and a still
/// reproducible.
struct SurfacePose {
  /// Where the board sits *on* the surface, in lattice cells - the whole board slid
  /// along, not the shape moved. Sliding one cell along the files puts a1 where b1 was;
  /// keep going and a1 arrives back at a1 having been all the way round, which on a
  /// Moebius band takes two laps and comes home mirrored. The surface functions are
  /// defined for every real `u` and `v` and already satisfy the variant's gluing there,
  /// so this is sampling, not a special case.
  float slideU{0.0f};
  float slideV{0.0f};
  /// Which side is out, for the play board: past the halfway point `PlaySurface` swaps
  /// the outward normal, so the pieces stand on the other face while the squares stay
  /// exactly where they were (M17.7, revised). The surface functions still *carry* a
  /// geometric eversion - the overtures use it - but the play board does not take it: it
  /// mirrors the whole shape about the origin and moves every cell, which is not a board.
  float evert{0.0f};
  /// How much of the Moebius ribbon's stretch to keep, 0 to 1, where 1 is the shape the
  /// library screen draws. The ribbon is the one surface in the catalogue whose two axes
  /// can be traded against each other freely - area is preserved either way - and the
  /// library spends all of it on the twist being unmistakable, which leaves each cell
  /// nine times longer than it is wide. A board has to be played on as well as looked at,
  /// so `PlaySurface` spends less. Nothing else reads it.
  float stretch{1.0f};
};

/// Where a lattice point lands on a derived overture's fully-formed surface, keyed by the
/// surface the geometry implies. Exposed so the differential test can assert it equals
/// the hand-authored surface for the same topology - the shipped scenes are the oracle
/// the generated ones are checked against.
[[nodiscard]] OvVec3 derivedSurfaceAt(app::SurfaceKind kind, float u, float v);

/// The same surface, posed. `SurfacePose{}` is the line above, to the bit.
[[nodiscard]] OvVec3 derivedSurfaceAt(app::SurfaceKind kind, float u, float v,
                                      SurfacePose pose);

/// Where a lattice point lands on an overture's fully-formed surface.
///
/// Exposed for one reason: seam closure is the property these warps exist to satisfy and
/// the only one that can be got wrong silently. `klein` joins its rank edges with the
/// file *reversed*, so (u, 0) must land on (1 - u, 1) - and a circular cross-section
/// cannot do that, it comes back shifted by four files instead. A test that cannot reach
/// the surface cannot tell those two apart, and the first version of this code shipped
/// the wrong one.
///
/// Overtures that form no closed surface return the flat board.
[[nodiscard]] OvVec3 overtureSurfaceAt(app::Overture which, float u, float v);

/// Whether a variant's play board has a surface to become (M17). True exactly when the
/// geometry names a non-flat 2-D surface; `standard`, mirrors and every non-glued variant
/// return false, and the interface must offer no toggle for them - a setting that did
/// nothing would be a lie.
[[nodiscard]] bool hasPlaySurface(const VariantSpec& v) noexcept;

/// A cell of a three- or four-dimensional variant placed on its hand-authored play shape
/// - the nested shells of `torus3d`, the tesseract of `hyper4` (M17.12) - in the same
/// Y-up world the two-dimensional surfaces use. False for anything without one.
///
/// Keyed by the variant's own identity, unlike the derived 2-D surfaces: a shape above
/// two dimensions is *authored*, not read off the gluing - `hyper4` is a plain 4-D box
/// with no identifications at all, and its shape is the projection a tesseract is always
/// drawn with. `PlaySurface` turns these positions into a tile per cell.
[[nodiscard]] bool playShapePosition(const VariantSpec& v, CellId cell, OvVec3& out);

/// Draw one, sorted far to near, into the rectangle it has been given.
///
/// `zoom` scales it about the rectangle's centre and `alpha` fades every colour, so the
/// shell's screen change can push the object away and take it with it - the same
/// contract `drawDeco` keeps. At alpha 0 it emits nothing at all.
void drawOverture(ImDrawList* dl, const OvertureScene& scene, ImVec2 min, ImVec2 max,
                  const view::Theme& theme, IconStyle iconStyle, float zoom = 1.0f,
                  float alpha = 1.0f);

}  // namespace cb::render
