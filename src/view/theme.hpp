// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace cb::view {

struct Rgba {
  float r{0}, g{0}, b{0}, a{1};

  static constexpr Rgba hex(std::uint32_t rgb, float alpha = 1.0f) {
    return Rgba{static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
                static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
  }
  [[nodiscard]] constexpr Rgba withAlpha(float alpha) const {
    return Rgba{r, g, b, alpha};
  }
  [[nodiscard]] constexpr std::array<float, 4> array() const { return {r, g, b, a}; }
};

/// The game's palette, as data.
///
/// Two themes ship. `manifold()` is the game's own look and the default: a bright, cool
/// instrument with the board several steps darker than the page, because the board is
/// what you came to look at and nothing else on screen may compete with it. `console()`
/// is the older candlelit one, kept because it is a second palette and a struct that
/// only ever holds one value is not really data.
///
/// The rule that survives both: **one accent, and geometry gets its own colours.**
/// `ember` is the accent - selection, headings, the primary action - and the seam ramp
/// is spent *only* where the board stops being flat. A player learns in one game that
/// those hues mean "this edge is not where it looks", and that only works if nothing
/// else ever uses them.
///
/// Being a struct rather than constants means a variant, or eventually a Workshop
/// package, can carry its own without anyone touching the renderer.
struct Theme {
  // Ground and structure. Brown-biased near-black, never a neutral grey: the scene
  // should read as candlelight on worn wood rather than as a developer tool.
  Rgba ink{Rgba::hex(0x0D0B0A)};
  Rgba soot{Rgba::hex(0x17130F)};
  Rgba panel{Rgba::hex(0x1E1813)};
  Rgba panelHi{Rgba::hex(0x2A211A)};
  Rgba rule{Rgba::hex(0x3A2E24)};

  // Warm side.
  Rgba bone{Rgba::hex(0xE8DCC4)};
  Rgba boneDim{Rgba::hex(0xA99A80)};
  Rgba boneFaint{Rgba::hex(0x6F6353)};
  Rgba ember{Rgba::hex(0xF0A23C)};
  Rgba emberDeep{Rgba::hex(0xA5601B)};

  // Cold side - reserved.
  Rgba rift{Rgba::hex(0x5FE3E0)};
  Rgba riftDeep{Rgba::hex(0x1E6F70)};

  // Seams. A glued edge is coloured by the portal it belongs to, swept along a cold
  // arc - green-cyan, through blue and violet, to magenta - so that the two ends of one
  // identification share a colour and a twist shows up as a reversed ramp. Hue is in
  // turns; the arc deliberately never reaches the warm half of the wheel, which belongs
  // to the game rather than to the geometry.
  float seamHueBegin{0.42f};
  float seamHueEnd{0.88f};
  float seamSaturation{0.72f};
  float seamValue{0.95f};
  /// A reflecting wall has nothing on the other side, so it gets no hue at all.
  Rgba mirrorEdge{Rgba::hex(0xCBD2DC)};

  // Semantic, deliberately separate from the accent.
  Rgba moss{Rgba::hex(0x8FB65C)};
  Rgba blood{Rgba::hex(0xD4483B)};

  // Lower contrast than a real chessboard, so pieces and marks stay the loudest thing -
  // but not so low that a piece disappears into the square it stands on. The old pair
  // (D9C9A8 / 5E4A34) put a pale piece on a pale square at 1.2:1, which a contrast test
  // caught; these are the nearest values in the same family that clear 2:1 all four ways.
  Rgba boardLight{Rgba::hex(0xA98F68)};
  Rgba boardDark{Rgba::hex(0x77614A)};

  /// The edge of the board as an object. Drawn under the cells so the board reads as a
  /// thing standing on the page rather than as a pattern printed on it.
  Rgba boardRim{Rgba::hex(0x241C15)};

  Rgba whitePiece{Rgba::hex(0xE8DCC4)};
  // Lifted off the true near-black of the ground: a black piece standing on a dark
  // cell has to stay a piece, not a hole.
  Rgba blackPiece{Rgba::hex(0x342A20)};

  /// True when the ground is lighter than the type. Anything that has to pick a
  /// contrasting colour - an outline on a pale piece, a scrim, a shadow - asks this
  /// rather than comparing luminances and guessing.
  bool light{false};

  [[nodiscard]] static Theme console() { return Theme{}; }

  /// The shell's own theme: a light instrument, a dark board.
  ///
  /// The two board values are not free choices. They were set by one constraint - both
  /// piece colours must read on both squares - and everything else was fitted around
  /// them.
  [[nodiscard]] static Theme manifold() {
    Theme t;
    t.ink = Rgba::hex(0xDCE0E6);  // the ground is now the light one
    t.soot = Rgba::hex(0xE6EAF0);
    t.panel = Rgba::hex(0xEEF1F6);
    t.panelHi = Rgba::hex(0xD2D9E4);
    t.rule = Rgba::hex(0xB4BECD);

    t.bone = Rgba::hex(0x0E121A);  // "bone" is now the ink: type on paper
    t.boneDim = Rgba::hex(0x39414F);
    t.boneFaint = Rgba::hex(0x737E90);
    t.ember = Rgba::hex(0x2E4BFF);  // the one accent, electric rather than warm
    t.emberDeep = Rgba::hex(0x6F4FF2);

    t.rift = Rgba::hex(0x1C7FA8);
    t.riftDeep = Rgba::hex(0x0E4A63);
    // A light page needs darker seams than a dark one, or the ramp burns out against it.
    t.seamValue = 0.74f;
    t.seamSaturation = 0.80f;
    t.mirrorEdge = Rgba::hex(0x8C96A8);

    t.moss = Rgba::hex(0x17937A);
    t.blood = Rgba::hex(0xD24A6A);

    t.boardLight = Rgba::hex(0x98A4BE);
    t.boardDark = Rgba::hex(0x59637E);
    t.boardRim = Rgba::hex(0x39415A);
    t.whitePiece = Rgba::hex(0xF6F9FF);
    t.blackPiece = Rgba::hex(0x10141E);
    t.light = true;
    return t;
  }
};

}  // namespace cb::view
