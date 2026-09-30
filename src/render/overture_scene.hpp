// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <imgui.h>

#include "app/overture.hpp"
#include "render/piece_icon.hpp"
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

/// Draw one, sorted far to near, into the rectangle it has been given.
///
/// `zoom` scales it about the rectangle's centre and `alpha` fades every colour, so the
/// shell's screen change can push the object away and take it with it - the same
/// contract `drawDeco` keeps. At alpha 0 it emits nothing at all.
void drawOverture(ImDrawList* dl, const OvertureScene& scene, ImVec2 min, ImVec2 max,
                  const view::Theme& theme, IconStyle iconStyle, float zoom = 1.0f,
                  float alpha = 1.0f);

}  // namespace cb::render
