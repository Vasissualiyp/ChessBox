// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ui_widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace cb::render::widgets {
namespace {

/// Widget chrome is sized from the current font, which the interface already scales for
/// the interface-scale option. A control therefore keeps its proportions at any scale
/// without every call site having to thread yet another number through.
float uiScale() {
  return std::max(0.5f, ImGui::GetFontSize() / 15.0f);
}

/// A vertical two-tone fill with rounded corners. Dear ImGui's rounded fill takes one
/// colour and its multi-colour fill takes no rounding, so a card needs both: a rounded
/// base, then a rounded-topped cap over the upper part.
void gradientRect(ImDrawList* dl, const ImVec2& a, const ImVec2& b, ImU32 top,
                  ImU32 bottom, float r) {
  dl->AddRectFilled(a, b, bottom, r);
  const float mid = a.y + (b.y - a.y) * 0.55f;
  dl->AddRectFilled(a, ImVec2(b.x, mid), top, r, ImDrawFlags_RoundCornersTop);
}

/// One card. Drawn rather than styled, because Dear ImGui will not give a window both a
/// thick dark outline and a bright inner rim.
void drawPlate(ImDrawList* dl, const ImVec2& pmin, const ImVec2& pmax,
               const view::Theme& t, float s) {
  const float r = 10.0f * s;
  gradientRect(dl, pmin, pmax, u32(t.panelHi), u32(t.panel), r);
  // A deep outline first, then a warm rim just inside it: the brass edging a game card
  // has, and the reason a plain ImGui window reads as a spreadsheet by comparison.
  dl->AddRect(pmin, pmax, u32(t.ink), r, 0, 3.0f * s);
  dl->AddRect(ImVec2(pmin.x + 2.5f * s, pmin.y + 2.5f * s),
              ImVec2(pmax.x - 2.5f * s, pmax.y - 2.5f * s), u32(t.emberDeep, 0.55f),
              r * 0.8f, 0, 1.4f * s);
  dl->AddLine(ImVec2(pmin.x + r * 0.9f, pmin.y + 4.0f * s),
              ImVec2(pmax.x - r * 0.9f, pmin.y + 4.0f * s), u32(t.bone, 0.10f), 1.0f * s);

  // Corner rivets set into the margin, so the plate looks fastened down.
  const float rr = 2.0f * s;
  const float in = 10.0f * s;
  const ImVec2 rivets[4] = {{pmin.x + in, pmin.y + in},
                            {pmax.x - in, pmin.y + in},
                            {pmin.x + in, pmax.y - in},
                            {pmax.x - in, pmax.y - in}};
  for (const ImVec2& c : rivets) dl->AddCircleFilled(c, rr, u32(t.ember, 0.7f));
}

}  // namespace

view::Rgba mix(const view::Rgba& a, const view::Rgba& b, float t) {
  return view::Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                    a.a + (b.a - a.a) * t};
}

view::Rgba shade(const view::Rgba& c, float factor) {
  return view::Rgba{c.r * factor, c.g * factor, c.b * factor, c.a};
}

void heading(const char* text, const view::Theme& theme, ImFont* font) {
  if (font != nullptr) ImGui::PushFont(font);
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.ember));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  if (font != nullptr) ImGui::PopFont();

  const ImVec2 max = ImGui::GetItemRectMax();
  const ImVec2 min = ImGui::GetItemRectMin();
  const float y = (min.y + max.y) * 0.5f;
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  if (right > max.x + 8.0f) {
    ImGui::GetWindowDrawList()->AddLine(ImVec2(max.x + 8.0f, y), ImVec2(right, y),
                                        u32(theme.emberDeep, 0.5f), 1.5f);
  }
  ImGui::Dummy(ImVec2(0, 2));
}

void keyValue(const char* key, const std::string& value, const view::Theme& theme,
              bool cold) {
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneFaint));
  ImGui::TextUnformatted(key);
  ImGui::PopStyleColor();
  ImGui::SameLine();
  const float avail = ImGui::GetContentRegionAvail().x;
  const float textWidth = ImGui::CalcTextSize(value.c_str()).x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, avail - textWidth));
  ImGui::PushStyleColor(ImGuiCol_Text, col(cold ? theme.rift : theme.bone));
  ImGui::TextUnformatted(value.c_str());
  ImGui::PopStyleColor();
}

namespace {

/// The shell's one interactive row. A menu entry and a Start/Back button are the same
/// control: type on a hairline with an accent wash under the pointer, never a filled
/// key. A list entry adds a number and a tail; a button adds neither. One drawing here
/// is what keeps the two from drifting apart again.
bool rowControl(const char* id, const char* label, const view::Theme& theme, ImFont* font,
                float width, float height, float scale, bool enabled, bool primary,
                bool cold, int index, const char* tail) {
  const float s = scale;
  // The stock button is only an input target: every pixel is drawn after it.
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  // The stock label is hidden; every pixel is drawn here instead.
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
  if (!enabled) ImGui::BeginDisabled();
  const bool pressed = ImGui::Button(id, ImVec2(width, height));
  if (!enabled) ImGui::EndDisabled();
  const bool hovered = enabled && ImGui::IsItemHovered();
  ImGui::PopStyleColor(5);

  // Everything is positioned from the item's *actual* rectangle rather than from the
  // cursor read before it: they are not always the same.
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // A wash that sweeps in from the left rather than a filled row: the row is type on a
  // page, and a solid block would turn the list back into a stack of buttons.
  const view::Rgba wash = cold ? theme.rift : theme.ember;
  if (hovered) {
    dl->AddRectFilledMultiColor(min, max, u32(wash, 0.16f), u32(wash, 0.0f),
                                u32(wash, 0.0f), u32(wash, 0.16f));
  }
  dl->AddLine(min, ImVec2(max.x, min.y), u32(theme.rule, 0.8f), 1.0f);

  const float labelHeight = font != nullptr ? font->FontSize : ImGui::GetFontSize();
  const float top = min.y + (max.y - min.y - labelHeight) * 0.5f;
  // The row steps towards the pointer, which is the only movement in the list.
  const float slide = hovered ? 12.0f * s : 0.0f;
  const float inset = index >= 0 ? 36.0f * s : 12.0f * s;
  const float textX = min.x + inset + slide;

  const ImU32 labelColor = u32(!enabled  ? theme.boneFaint
                               : primary ? theme.ember
                               : cold    ? theme.rift
                               : hovered ? theme.ember
                                         : theme.bone);
  const std::string caption = upper(label);
  if (font != nullptr) {
    const float small = font->FontSize * 0.58f;
    if (index >= 0) {
      char idx[4]{};
      std::snprintf(idx, sizeof(idx), "%02d", std::clamp(index, 0, 99));
      dl->AddText(font, small,
                  ImVec2(min.x + 6.0f * s, min.y + (max.y - min.y - small) * 0.5f),
                  u32(theme.boneFaint), idx);
    }
    dl->AddText(font, labelHeight, ImVec2(textX, top), labelColor, caption.c_str());
    if (tail != nullptr) {
      const ImVec2 w = font->CalcTextSizeA(small, FLT_MAX, 0.0f, tail);
      dl->AddText(font, small,
                  ImVec2(max.x - 8.0f * s - w.x, min.y + (max.y - min.y - small) * 0.5f),
                  u32(enabled ? (hovered ? theme.ember : theme.rule) : theme.boneFaint),
                  tail);
    }
  } else {
    dl->AddText(ImVec2(textX, top), labelColor, caption.c_str());
  }
  return pressed && enabled;
}

}  // namespace

bool button(const char* label, const view::Theme& theme, float width, bool primary,
            bool cold, bool enabled, ImFont* font) {
  const float s = uiScale();
  return rowControl(label, label, theme, font, width, 42.0f * s, s, enabled, primary,
                    cold, -1, nullptr);
}

void eyebrow(const char* text, const view::Theme& theme, ImFont* monoFont, float scale) {
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float size = (monoFont != nullptr ? monoFont->FontSize : ImGui::GetFontSize());
  const std::string caption = upper(text);
  float textW = 0;
  if (monoFont != nullptr) {
    textW = monoFont->CalcTextSizeA(size, FLT_MAX, 0.0f, caption.c_str()).x;
    dl->AddText(monoFont, size, at, u32(theme.ember), caption.c_str());
  } else {
    textW = ImGui::CalcTextSize(caption.c_str()).x;
    dl->AddText(at, u32(theme.ember), caption.c_str());
  }
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  const float y = at.y + size * 0.5f;
  if (right > at.x + textW + 10.0f * scale) {
    dl->AddLine(ImVec2(at.x + textW + 10.0f * scale, y), ImVec2(right, y),
                u32(theme.rule), 1.0f);
  }
  ImGui::Dummy(ImVec2(0, size + 6.0f * scale));
}

void screenTitle(const char* text, const view::Theme& theme, ImFont* displayFont,
                 float scale, float sizeMul) {
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const std::string caption = upper(text);
  const float base =
      displayFont != nullptr ? displayFont->FontSize : ImGui::GetFontSize();
  const float size = base * sizeMul;
  if (displayFont != nullptr) {
    dl->AddText(displayFont, size, at, u32(theme.bone), caption.c_str());
  } else {
    dl->AddText(at, u32(theme.bone), caption.c_str());
  }
  ImGui::Dummy(ImVec2(0, size + 4.0f * scale));
}

bool menuEntry(const char* label, int index, const view::Theme& theme, ImFont* labelFont,
               float width, bool enabled, float scale) {
  const std::string id = "##" + std::string(label);
  // What is coming stays in the list and says so, in two words at the far end of the
  // row. It does not need a paragraph underneath it every time the pointer goes past.
  return rowControl(id.c_str(), label, theme, labelFont, width, 42.0f * scale, scale,
                    enabled, false, false, index, enabled ? ">" : "NOT BUILT");
}

namespace {

/// How tall each pane's content was last frame, so it can be centred this frame.
///
/// Measured rather than declared: the content is laid out by ImGui and depends on the
/// interface scale, the fonts and the text itself, so the only honest height is the one
/// it actually came out at. One frame of lag is invisible; a menu whose last row falls
/// off the bottom at a large interface scale is not.
std::map<std::string, float>& paneHeights() {
  static std::map<std::string, float> heights;
  return heights;
}
std::string& currentPane() {
  static std::string id;
  return id;
}
/// Where this pane's content actually began. Subtracted again when the height is
/// recorded, or centring would feed back into its own measurement and creep down the
/// screen a little further every frame.
float& currentPaneTop() {
  static float top = 0;
  return top;
}
/// Whether this pane's height may be remembered. False for a departing ghost.
bool& currentPaneRemember() {
  static bool remember = true;
  return remember;
}

}  // namespace

void beginPane(const char* id, ImVec2 min, ImVec2 max, float scale, float alpha,
               float pad, bool remember) {
  const float k = std::max(0.05f, scale);
  const float a = std::clamp(alpha, 0.0f, 1.0f);
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const ImVec2 size((max.x - min.x - pad * 2.0f) * k, (max.y - min.y - pad * 2.0f) * k);

  ImGui::SetNextWindowPos(centre, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBackground);
  // Text has to travel with the pane or only the boxes move.
  ImGui::SetWindowFontScale(k);

  currentPane() = id;
  currentPaneRemember() = remember;
  currentPaneTop() = ImGui::GetCursorPosY();
  const auto it = paneHeights().find(id);
  if (it != paneHeights().end() && it->second < size.y) {
    currentPaneTop() = (size.y - it->second) * 0.5f;
    ImGui::SetCursorPosY(currentPaneTop());
  }
}

void endPane() {
  if (currentPaneRemember()) {
    paneHeights()[currentPane()] = ImGui::GetCursorPosY() - currentPaneTop();
  }
  ImGui::SetWindowFontScale(1.0f);
  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

void beginPlate(const char* id, const view::Theme& theme, ImVec2 size, float yBias) {
  const float s = uiScale();
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                 vp->WorkPos.y + vp->WorkSize.y * yBias),
                          ImGuiCond_Always, ImVec2(0.5f, yBias));
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * s, 22.0f * s));
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
  // The card is drawn first so content lands on top of it. For an auto-height plate the
  // size is the previous frame's, which settles on the frame after a screen opens.
  const ImVec2 p = ImGui::GetWindowPos();
  const ImVec2 sz = ImGui::GetWindowSize();
  drawPlate(ImGui::GetWindowDrawList(), p, ImVec2(p.x + sz.x, p.y + sz.y), theme, s);
}

void endPlate() {
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

}  // namespace cb::render::widgets
