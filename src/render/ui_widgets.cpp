// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ui_widgets.hpp"

#include <algorithm>

namespace cb::render::widgets {

void heading(const char* text, const view::Theme& theme, ImFont* font) {
  if (font != nullptr) ImGui::PushFont(font);
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneDim));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  if (font != nullptr) ImGui::PopFont();

  const ImVec2 max = ImGui::GetItemRectMax();
  const ImVec2 min = ImGui::GetItemRectMin();
  const float y = (min.y + max.y) * 0.5f;
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  if (right > max.x + 8.0f) {
    ImGui::GetWindowDrawList()->AddLine(ImVec2(max.x + 8.0f, y), ImVec2(right, y),
                                        u32(theme.rule), 1.0f);
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
  const view::Rgba accent = cold ? theme.rift : theme.ember;
  ImGui::PushStyleColor(ImGuiCol_Button, primary ? col(accent) : col(theme.panel));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        primary ? col(accent, 0.86f) : col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(accent));
  ImGui::PushStyleColor(
      ImGuiCol_Text, primary ? col(theme.ink) : col(cold ? theme.rift : theme.boneDim));
  ImGui::PushStyleColor(ImGuiCol_Border,
                        primary ? col(accent) : col(cold ? theme.riftDeep : theme.rule));
  if (!enabled) ImGui::BeginDisabled();
  const bool pressed = ImGui::Button(label, ImVec2(width, 0));
  if (!enabled) ImGui::EndDisabled();
  ImGui::PopStyleColor(5);

  // A hard drop shadow rather than a blur: the control should look like it sits on the
  // panel and could be pressed into it.
  if (enabled) {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x + 1, max.y), ImVec2(max.x, max.y),
                                        u32(theme.ink), 2.0f);
  }
  return pressed;
}

bool menuEntry(const char* label, const char* note, const view::Theme& theme,
               ImFont* labelFont, ImFont* noteFont, float width, bool enabled,
               float scale) {
  const bool hasNote = note != nullptr && *note != '\0';
  const float height = (hasNote ? 54.0f : 42.0f) * scale;

  ImGui::PushStyleColor(ImGuiCol_Button, col(theme.panel, enabled ? 1.0f : 0.5f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(theme.emberDeep));
  ImGui::PushStyleColor(ImGuiCol_Border, col(theme.rule));
  const bool pressed =
      ImGui::Button(("##" + std::string(label)).c_str(), ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();
  ImGui::PopStyleColor(4);

  // Everything is positioned from the item's *actual* rectangle rather than from the
  // cursor read before it. They are not always the same - style padding and alignment
  // can move the item - and drawing from the stale position leaves the lit edge floating
  // beside the row instead of on it.
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // A lit left edge marks what the pointer is on, rather than a filled row that would
  // fight the plate it sits in.
  dl->AddRectFilled(min, ImVec2(min.x + 3.0f * scale, max.y),
                    u32(enabled && hovered ? theme.ember : theme.rule));

  // Lay the text out from the fonts' own metrics, so an entry stays centred at any scale.
  const float labelHeight =
      labelFont != nullptr ? labelFont->FontSize : ImGui::GetFontSize();
  const float noteHeight =
      noteFont != nullptr ? noteFont->FontSize : ImGui::GetFontSize();
  const float gap = 4.0f * scale;
  const float block = hasNote ? labelHeight + gap + noteHeight : labelHeight;
  const float top = min.y + ((max.y - min.y) - block) * 0.5f;
  const float textX = min.x + 16.0f * scale;

  const ImU32 labelColor =
      u32(enabled ? (hovered ? theme.bone : theme.boneDim) : theme.boneFaint);
  const std::string caption = upper(label);
  if (labelFont != nullptr) {
    dl->AddText(labelFont, labelFont->FontSize, ImVec2(textX, top), labelColor,
                caption.c_str());
  } else {
    dl->AddText(ImVec2(textX, top), labelColor, caption.c_str());
  }
  if (hasNote) {
    const ImVec2 notePos(textX, top + labelHeight + gap);
    if (noteFont != nullptr) {
      dl->AddText(noteFont, noteFont->FontSize, notePos, u32(theme.boneFaint), note);
    } else {
      dl->AddText(notePos, u32(theme.boneFaint), note);
    }
  }
  return pressed && enabled;
}

void beginPlate(const char* id, const view::Theme& theme, ImVec2 size, float yBias) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                 vp->WorkPos.y + vp->WorkSize.y * yBias),
                          ImGuiCond_Always, ImVec2(0.5f, yBias));
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, col(theme.soot, 0.97f));
  ImGui::PushStyleColor(ImGuiCol_Border, col(theme.rule));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 20));
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoSavedSettings);
}

void endPlate() {
  ImGui::End();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

}  // namespace cb::render::widgets
