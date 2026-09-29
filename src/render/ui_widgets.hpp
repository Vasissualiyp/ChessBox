// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include <imgui.h>

#include "view/theme.hpp"

/// Small drawing helpers shared by the game's HUD and its menus.
///
/// They exist because Dear ImGui's stock widgets are built for tools: a plain Button is
/// a flat tint and a plain header is just text. The game needs controls that look
/// pressable and plates that look machined, and each is a few lines of draw-list work.
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

/// A section heading with a rule running out to the right, the way a plate is labelled.
void heading(const char* text, const view::Theme& theme, ImFont* font);

/// A right-aligned label and value pair, for readouts.
void keyValue(const char* key, const std::string& value, const view::Theme& theme,
              bool cold = false);

/// A chunky bevelled control. "primary" fills it with the accent; "cold" marks a
/// control that acts on geometry or time rather than on the game.
bool button(const char* label, const view::Theme& theme, float width = 0.0f,
            bool primary = false, bool cold = false, bool enabled = true);

/// A large menu entry. Disabled entries stay visible and carry their reason - a menu
/// that hides what is coming is dishonest, and one that pretends is worse.
/// `scale` is required rather than defaulted: a missed call site would silently draw one
/// row of a menu at a different size from the rest, which is exactly the bug that made
/// the main menu look ragged.
bool menuEntry(const char* label, const char* note, const view::Theme& theme,
               ImFont* labelFont, ImFont* noteFont, float width, bool enabled,
               float scale);

/// A framed panel centred in the viewport, used by every screen that sits over the board.
void beginPlate(const char* id, const view::Theme& theme, ImVec2 size,
                float yBias = 0.5f);
void endPlate();

}  // namespace cb::render::widgets
