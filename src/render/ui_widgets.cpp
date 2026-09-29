// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ui_widgets.hpp"

#include <algorithm>
#include <cmath>

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

bool button(const char* label, const view::Theme& theme, float width, bool primary,
            bool cold, bool enabled) {
  const float s = uiScale();
  const view::Rgba accent = cold ? theme.rift : theme.ember;
  // The stock button is only an input target: every pixel is drawn after it, so the
  // control can be a slab with an outline instead of a flat tint.
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
  if (!enabled) ImGui::BeginDisabled();
  const bool pressed = ImGui::Button(label, ImVec2(width, 0));
  if (!enabled) ImGui::EndDisabled();
  const bool hovered = enabled && ImGui::IsItemHovered();
  const bool active = enabled && ImGui::IsItemActive();
  ImGui::PopStyleColor(5);

  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // A press pushes the face down into its own shadow, which is most of what makes a
  // flat rectangle feel like a physical key.
  const float press = active ? 2.0f * s : 0.0f;
  const ImVec2 tl(min.x, min.y + press);
  const ImVec2 br(max.x, max.y + press);
  const float r = 7.0f * s;

  view::Rgba face = primary ? accent : theme.panelHi;
  if (!enabled) {
    face = mix(face, theme.ink, 0.45f);
  } else if (active) {
    face = mix(face, theme.ink, 0.16f);
  } else if (hovered) {
    face = mix(face, theme.bone, primary ? 0.10f : 0.14f);
  }

  dl->AddRectFilled(ImVec2(tl.x + 2.0f * s, tl.y + 3.0f * s),
                    ImVec2(br.x + 2.0f * s, br.y + 3.0f * s), u32(theme.ink, 0.55f), r);
  gradientRect(dl, tl, br, u32(mix(face, theme.bone, 0.10f)), u32(shade(face, 0.82f)), r);
  dl->AddRect(tl, br, u32(theme.ink), r, 0, 2.5f * s);
  dl->AddLine(ImVec2(tl.x + r * 0.7f, tl.y + 2.2f * s),
              ImVec2(br.x - r * 0.7f, tl.y + 2.2f * s),
              u32(primary ? theme.ink : theme.bone, primary ? 0.25f : 0.14f), 1.2f * s);

  const std::string cap = upper(label);
  const ImVec2 ts = ImGui::CalcTextSize(cap.c_str());
  const ImU32 textCol =
      u32(!enabled ? theme.boneFaint
                   : (primary ? theme.ink : (cold ? theme.rift : theme.bone)));
  dl->AddText(
      ImVec2((tl.x + br.x) * 0.5f - ts.x * 0.5f, (tl.y + br.y) * 0.5f - ts.y * 0.5f),
      textCol, cap.c_str());
  return pressed && enabled;
}

bool menuEntry(const char* label, const view::Theme& theme, ImFont* labelFont,
               float width, bool enabled, float scale) {
  const float height = 54.0f * scale;

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
  const bool pressed =
      ImGui::Button(("##" + std::string(label)).c_str(), ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();
  ImGui::PopStyleColor(4);

  // Everything is positioned from the item's *actual* rectangle rather than from the
  // cursor read before it: they are not always the same, and drawing from the stale
  // position leaves the lit edge floating beside the row instead of on it.
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float s = scale;
  const float r = 6.0f * s;

  view::Rgba face = theme.panel;
  if (!enabled) {
    face = mix(face, theme.ink, 0.4f);
  } else if (hovered) {
    face = mix(theme.panelHi, theme.ember, 0.12f);
  }
  dl->AddRectFilled(ImVec2(min.x + 2.0f * s, min.y + 3.0f * s),
                    ImVec2(max.x + 2.0f * s, max.y + 3.0f * s), u32(theme.ink, 0.5f), r);
  gradientRect(dl, min, max, u32(mix(face, theme.bone, 0.06f)), u32(shade(face, 0.84f)),
               r);
  dl->AddRect(min, max, u32(theme.ink), r, 0, 2.0f * s);
  // A lit bar on the left marks what the pointer is on; a filled row would fight the
  // card it sits in.
  dl->AddRectFilled(ImVec2(min.x, min.y + r), ImVec2(min.x + 4.0f * s, max.y - r),
                    u32(enabled && hovered ? theme.ember : theme.rule), 0.0f);

  const float labelHeight =
      labelFont != nullptr ? labelFont->FontSize : ImGui::GetFontSize();
  const float top = min.y + (max.y - min.y - labelHeight) * 0.5f;
  const float textX = min.x + 20.0f * s;
  const std::string caption = upper(label);
  const ImU32 labelColor =
      u32(!enabled ? theme.boneFaint : (hovered ? theme.bone : theme.boneDim));
  if (labelFont != nullptr) {
    dl->AddText(labelFont, labelFont->FontSize, ImVec2(textX, top), labelColor,
                caption.c_str());
  } else {
    dl->AddText(ImVec2(textX, top), labelColor, caption.c_str());
  }
  return pressed && enabled;
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
                   ImGuiWindowFlags_NoSavedSettings);
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
