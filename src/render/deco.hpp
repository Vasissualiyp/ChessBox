// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include <imgui.h>

#include "render/piece_icon.hpp"
#include "variant/variant.hpp"
#include "view/theme.hpp"

namespace cb::render {

/// The objects the shell puts beside its menus.
///
/// Every screen shows one, and each is a real thing rather than an ornament: the
/// surface people draw when they explain string theory, the lattice the chosen variant
/// actually plays on, a four-dimensional cube, a move atom expanding into the
/// directions it stands for. They are projected on the CPU and drawn as polygons
/// through the interface's own draw list, so they cost a pipeline of nothing and can be
/// captured headlessly like everything else.
enum class Deco : std::uint8_t {
  None,
  /// A Calabi-Yau quintic cross-section, tiled in semi-transparent black and white: a
  /// board wrapped onto a shape a board cannot be.
  Manifold,
  /// The selected variant's own boards, as a turning grid.
  Lattice,
  /// A 4-cube, turning in two planes at once.
  Tesseract,
  /// One move atom, expanding into every direction its magnitudes permit.
  Atom,
};

/// Which object belongs beside which screen. A free function so the mapping is stated
/// once and can be read without opening the menu code.
Deco decoForScreen(int screen) noexcept;

/// The polygon field behind everything, and its memory of the camera.
///
/// The polygons have a real depth and are projected, rather than being sprites that
/// happen to drift: that is what lets them answer a menu move. `push` adds an impulse
/// to the whole field's z velocity, and the field swells and sweeps outward past the
/// frame as the camera goes in.
class DepthField {
 public:
  DepthField();

  /// Move the field on. `dt` is seconds.
  void advance(float dt);
  /// Dolly the field. Positive goes towards the viewer, which is what a menu move into
  /// a deeper screen looks like; negative is backing out of one.
  void push(float amount) noexcept { velocity_ += amount; }

  /// Draw it into `dl`, filling the given rectangle.
  ///
  /// `iconStyle` is the flat-piece set, because the drifting pieces are the same shapes
  /// the board uses - the background is made of the game, not of decoration.
  void draw(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
            IconStyle iconStyle, bool withPieces, bool withPolygons) const;

 private:
  struct Body {
    float px{0}, py{0};   ///< position on the plane, in units of half the frame
    float z{1};           ///< distance from the camera
    float zDrift{0};
    float angle{0}, angleV{0};
    float radius{0}, radiusV{0}, radiusPhase{0};
    float spin{0}, spinV{0};
    float size{0};
    float alphaPhase{0}, alphaV{0};
    float huePhase{0}, hueV{0};
    std::uint8_t colorA{0}, colorB{0};
    std::uint8_t shape{0};  ///< index into the piece archetypes, for the mesh bodies
    bool piece{false};
  };
  void respawn(Body& b, std::uint32_t& seed, bool nearPlane) const;

  std::vector<Body> bodies_;
  float velocity_{0};
  float clock_{0};
};

/// Draw one decorative object into a rectangle. `variant` may be null; only the lattice
/// uses it, and with nothing loaded it falls back to a single board.
void drawDeco(ImDrawList* dl, Deco what, ImVec2 min, ImVec2 max, const view::Theme& theme,
              IconStyle iconStyle, float time, const VariantSpec* variant);

}  // namespace cb::render
