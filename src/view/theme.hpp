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
/// One rule holds the whole look together: **warm is the game you know, cold is the
/// geometry you don't.** `ember` is the candle - selection, headings, the primary
/// action - and `rift` is spent *only* where the board stops being flat: seams,
/// wrapped moves, timelines, extra axes. A player learns in one game that cyan means
/// "this edge is not where it looks", and that only works if nothing else ever uses it.
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

  // Lower contrast than a real chessboard, so pieces and marks stay the loudest thing.
  Rgba boardLight{Rgba::hex(0xD9C9A8)};
  Rgba boardDark{Rgba::hex(0x5E4A34)};

  Rgba whitePiece{Rgba::hex(0xE8DCC4)};
  // Lifted off the true near-black of the ground: a black piece standing on a dark
  // cell has to stay a piece, not a hole.
  Rgba blackPiece{Rgba::hex(0x342A20)};

  [[nodiscard]] static Theme console() { return Theme{}; }
};

}  // namespace cb::view
