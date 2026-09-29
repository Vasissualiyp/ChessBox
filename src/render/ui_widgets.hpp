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

/// An action, drawn as a menu row: type on a hairline with an ember wash under the
/// pointer, never a filled key - so a Start/Back pair and the main menu are one control.
/// "primary" sets the label in the accent; "cold" marks a control that acts on geometry
/// or time rather than on the game; `font` is the label typeface, the same one a menu
/// entry passes, or null for the interface's body font.
bool button(const char* label, const view::Theme& theme, float width = 0.0f,
            bool primary = false, bool cold = false, bool enabled = true,
            ImFont* font = nullptr);

/// The height a `button` row draws at, so a layout can reserve room for one and the row
/// is never the thing that gets clipped when the interface scale grows.
[[nodiscard]] float controlHeight();

/// A large menu entry. The label is the whole entry: a control says what it is by being
/// named, and a line of grey text under every row is exactly the developer-tool look
/// this interface is trying not to have. Disabled entries stay visible.
/// A small monospaced label with a rule running out to the right. Marks what screen you
/// are on without spending the title's weight on it.
void eyebrow(const char* text, const view::Theme& theme, ImFont* monoFont, float scale);

/// The screen's name, set large. Drawn through the draw list rather than as ordinary
/// text because ImGui has one size per font, and a title wants several times the body.
void screenTitle(const char* text, const view::Theme& theme, ImFont* displayFont,
                 float scale, float sizeMul = 2.1f);

/// A menu row: type on a hairline, not a box.
///
/// `index` is the row's number, set small and monospaced at the left - it says the list
/// is ordered and gives the eye somewhere to start. `scale` is required rather than
/// defaulted: a missed call site would silently draw one row at a different size from
/// the rest, which is exactly the bug that once made the main menu look ragged.
bool menuEntry(const char* label, int index, const view::Theme& theme, ImFont* labelFont,
               float width, bool enabled, float scale);

/// A framed card centred in the viewport, used by every screen that sits over the board.
/// One half of the shell: the menu column beside the screen's object.
///
/// `scale` grows or shrinks the whole card and its text about the pane's centre, and
/// `alpha` fades it. Both are supplied by the shell's transition rather than derived
/// here, so an arriving screen can grow from small while a departing one swells and
/// fades, and the two moves can be exact reverses of each other. `remember` is false for
/// a departing ghost: its scaled measurement must not become the height the settled pane
/// is later centred from.
void beginPane(const char* id, ImVec2 min, ImVec2 max, float scale, float alpha,
               float pad, bool remember = true);
void endPane();

void beginPlate(const char* id, const view::Theme& theme, ImVec2 size,
                float yBias = 0.5f);
void endPlate();

}  // namespace cb::render::widgets
