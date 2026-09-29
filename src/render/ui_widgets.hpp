// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include <imgui.h>

#include "view/theme.hpp"

/// Small drawing helpers shared by the game's HUD and its menus.
///
/// They exist because Dear ImGui's stock widgets are built for tools: a plain Button is
/// a flat tint and a plain header is just text. The game wants the opposite of a tool -
/// chunky cards, hard bevels, a drop shadow you could stand a coin on - so each control
/// here is a real draw-list sprite with the stock widget underneath only for input.
namespace cb::render::widgets {

inline ImVec4 col(const view::Rgba& c, float alpha = 1.0f) {
  return ImVec4{c.r, c.g, c.b, c.a * alpha};
}
inline ImU32 u32(const view::Rgba& c, float alpha = 1.0f) {
  return ImGui::ColorConvertFloat4ToU32(col(c, alpha));
}

inline std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}

/// Blend two colours, alpha included.
view::Rgba mix(const view::Rgba& a, const view::Rgba& b, float t);
/// Multiply a colour's brightness without touching its alpha.
view::Rgba shade(const view::Rgba& c, float factor);

/// A section heading with a rule running out to the right, the way a plate is labelled.
void heading(const char* text, const view::Theme& theme, ImFont* font);

/// A right-aligned label and value pair, for readouts.
void keyValue(const char* key, const std::string& value, const view::Theme& theme,
              bool cold = false);

/// A chunky bevelled control. "primary" fills it with the accent; "cold" marks a
/// control that acts on geometry or time rather than on the game.
bool button(const char* label, const view::Theme& theme, float width = 0.0f,
            bool primary = false, bool cold = false, bool enabled = true);

/// A large menu entry. The label is the whole entry: a control says what it is by being
/// named, and a line of grey text under every row is exactly the developer-tool look
/// this interface is trying not to have. Disabled entries stay visible.
bool menuEntry(const char* label, const view::Theme& theme, ImFont* labelFont,
               float width, bool enabled, float scale);

/// A framed card centred in the viewport, used by every screen that sits over the board.
void beginPlate(const char* id, const view::Theme& theme, ImVec2 size,
                float yBias = 0.5f);
void endPlate();

}  // namespace cb::render::widgets
