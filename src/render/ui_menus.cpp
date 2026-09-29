// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every screen that is not the board: the main menu, the variant picker, pause,
// settings, the editor, and the two reference screens.
//
// They read state from app::Shell and report intent back through UiRequest. Nothing
// here decides anything - which is the same rule the board follows, and is why the
// transitions between screens are covered by tests that never open a window.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "io/notation.hpp"
#include "render/piece_mesh.hpp"
#include "render/ui.hpp"
#include "render/ui_widgets.hpp"

namespace cb::render {
namespace {

using namespace widgets;

/// A short, human description of a board's shape - the kind of thing a player wants
/// before starting a game, not a list of axis extents.
std::string describeBoard(const VariantSpec& v) {
  std::string s = std::to_string(v.dims.dims()) + " axes, " +
                  std::to_string(v.dims.cellCount()) + " cells";
  if (!v.geom.isBox()) {
    s += v.geom.isOrientable() ? ", glued" : ", glued with a twist";
  }
  return s;
}

/// Where the cells a piece can reach land, projected onto the first two axes.
///
/// It is the most useful thing the game can show about a variant nobody has played:
/// the atoms are exact but abstract, and a diagram of the squares is immediately
/// readable. Anything the piece can only do by leaving the plane is noted rather than
/// silently dropped.
void drawMoveDiagram(const VariantSpec& v, const PieceTypeDef& piece,
                     const view::Theme& theme, float cellSize) {
  constexpr int kRadius = 4;
  const int span = kRadius * 2 + 1;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();

  // 0 = unreachable, 1 = by sliding, 2 = by leaping.
  std::vector<int> mark(static_cast<std::size_t>(span * span), 0);
  bool leavesPlane = false;

  for (const MoveAtom& atom : piece.atoms) {
    const auto begin = atom.dirBegin[static_cast<std::size_t>(Color::White)];
    const auto end = atom.dirEnd[static_cast<std::size_t>(Color::White)];
    for (std::uint32_t di = begin; di < end && di < v.dirTable.size(); ++di) {
      const Direction& d = v.dirTable[di];
      bool planar = true;
      for (std::uint8_t a = 2; a < d.n; ++a) {
        if (d.v[a] != 0) planar = false;
      }
      if (!planar) {
        leavesPlane = true;
        continue;
      }
      const std::uint32_t steps = atom.maxK == kUnlimited
                                      ? static_cast<std::uint32_t>(kRadius)
                                      : std::min<std::uint32_t>(atom.maxK, kRadius);
      for (std::uint32_t k = atom.minK; k <= steps; ++k) {
        const int dx = d.v[0] * static_cast<int>(k);
        const int dy = d.v[1] * static_cast<int>(k);
        if (std::abs(dx) > kRadius || std::abs(dy) > kRadius) break;
        const int gx = dx + kRadius;
        const int gy = kRadius - dy;  // drawn with rank increasing upward
        mark[static_cast<std::size_t>(gy * span + gx)] =
            atom.mode == MoveMode::Slide ? 1 : 2;
      }
    }
  }

  for (int y = 0; y < span; ++y) {
    for (int x = 0; x < span; ++x) {
      const ImVec2 a(origin.x + static_cast<float>(x) * cellSize,
                     origin.y + static_cast<float>(y) * cellSize);
      const ImVec2 b(a.x + cellSize - 1.0f, a.y + cellSize - 1.0f);
      const bool light = (x + y) % 2 == 0;
      dl->AddRectFilled(a, b, u32(light ? theme.boardLight : theme.boardDark, 0.35f));

      const int m = mark[static_cast<std::size_t>(y * span + x)];
      if (x == kRadius && y == kRadius) {
        dl->AddRectFilled(a, b, u32(theme.ember, 0.85f));
      } else if (m == 1) {
        dl->AddCircleFilled(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f),
                            cellSize * 0.22f, u32(theme.moss));
      } else if (m == 2) {
        // A leap is drawn as a ring: it arrives without crossing what lies between.
        dl->AddCircle(ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), cellSize * 0.26f,
                      u32(theme.moss), 0, 2.0f);
      }
    }
  }
  const float side = static_cast<float>(span) * cellSize;
  dl->AddRect(origin, ImVec2(origin.x + side, origin.y + side), u32(theme.rule));
  ImGui::Dummy(ImVec2(side, side));

  if (leavesPlane) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(theme.rift));
    ImGui::TextUnformatted("also leaves this plane");
    ImGui::PopStyleColor();
  }
}

}  // namespace

UiRequest Ui::buildMainMenu(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  beginPane("##mainmenu", menuMin, menuMax, enter_, px(46.0f));
  eyebrow("chessbox", t, mono, scale_);
  screenTitle("Every board", t, display, scale_, 2.4f);
  screenTitle("you can define.", t, display, scale_, 2.4f);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextWrapped(
      "Any number of axes. Any gluing. Pieces as vectors. Time as another direction to "
      "move in.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(14)));

  const float width = ImGui::GetContentRegionAvail().x;
  if (menuEntry("New game", 1, t, display, width, true, scale_)) {
    shell.go(app::Screen::NewGame);
  }
  // Disabled rows stay in the list, marked and nothing more. A menu that hides what is
  // coming is dishonest; one that explains itself every time the pointer crosses it is
  // just noisy.
  if (menuEntry("Continue", 2, t, display, width, false, scale_)) {
    // unreachable while disabled, but kept so enabling it is a one-word change
  }
  if (menuEntry("Multiplayer", 3, t, display, width, false, scale_)) {
  }
  if (menuEntry("Editor", 4, t, display, width, true, scale_)) {
    shell.go(app::Screen::Editor);
  }
  if (menuEntry("Settings", 5, t, display, width, true, scale_)) {
    shell.go(app::Screen::Settings);
  }
  if (menuEntry("Quit", 6, t, display, width, true, scale_)) request.quit = true;

  endPane();
  return request;
}

UiRequest Ui::buildNewGame(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  // Settled before the frame is drawn: the decoration beside the menu is the selected
  // variant's own lattice, and it is drawn first.
  if (pickedVariant_.empty()) {
    pickedVariant_ = shell.currentVariant();
    if (pickedVariant_.empty() && !shell.library().empty()) {
      pickedVariant_ = shell.library().front();
    }
  }

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  beginPane("##newgame", menuMin, menuMax, enter_, px(46.0f));
  eyebrow("new game", t, mono, scale_);
  screenTitle("Library", t, display, scale_, 2.2f);
  ImGui::Dummy(ImVec2(0, px(6)));
  // The description lives in the variant file, so it is read once per selection rather
  // than every frame. A variant that will not load simply has no blurb.
  if (describedFor_ != pickedVariant_) {
    describedFor_ = pickedVariant_;
    pickedDescription_ = shell.variantDescription(pickedVariant_);
  }

  const float rowH = px(26.0f);
  // Proportional, not a pixel count: the pane is half the window, and a width chosen
  // for a fixed plate squeezes the description to one letter per line.
  const float listW = ImGui::GetContentRegionAvail().x * 0.44f;
  const float listH = ImGui::GetContentRegionAvail().y - px(70.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, col(t.ink, 0.45f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::BeginChild("##library", ImVec2(listW, listH), ImGuiChildFlags_Border);
  for (const std::string& name : shell.library()) {
    const bool active = name == pickedVariant_;
    ImGui::PushID(name.c_str());
    const float rowW = ImGui::GetContentRegionAvail().x;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + rowW, min.y + rowH);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
      dl->AddRectFilled(min, max, u32(t.emberDeep), px(4));
      dl->AddRectFilled(ImVec2(min.x, min.y + px(3)),
                        ImVec2(min.x + px(4), max.y - px(3)), u32(t.ember));
    } else if (hovered) {
      dl->AddRectFilled(min, max, u32(t.panelHi), px(4));
    }
    const ImU32 textCol = u32(active ? t.ink : (hovered ? t.bone : t.boneDim));
    dl->AddText(ImVec2(min.x + px(12), min.y + (rowH - ImGui::GetFontSize()) * 0.5f),
                textCol, upper(name).c_str());
    if (clicked) pickedVariant_ = name;
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  ImGui::SameLine();
  ImGui::BeginChild("##details", ImVec2(0, listH));
  ImGui::PushFont(display);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.bone));
  ImGui::TextUnformatted(upper(pickedVariant_).c_str());
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(6)));
  // The board's shape, once the game is actually loaded - real numbers rather than a
  // promise. Before that the variant's own one-line pitch is the honest thing to show.
  if (const app::Session* loaded = shell.session();
      loaded != nullptr && loaded->variant().name == pickedVariant_) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextUnformatted(describeBoard(loaded->variant()).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(8)));
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  if (!pickedDescription_.empty()) {
    ImGui::TextWrapped("%s", pickedDescription_.c_str());
  }
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::EndChild();

  ImGui::Dummy(ImVec2(0, px(8)));
  if (button("START", t, px(150), true)) request.loadVariant = pickedVariant_;
  ImGui::SameLine();
  if (button("BACK", t, px(110))) shell.back();
  if (!shell.message().empty()) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
    ImGui::TextWrapped("%s", shell.message().c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  endPane();
  return request;
}

UiRequest Ui::buildPause(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  // Pause does not cover the board - the whole point of stepping the camera back and
  // throwing the position out of focus is that you can still see it. So the menu takes
  // one side, over a scrim thin enough to read through, and the board stays where it is.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 vmin = vp->WorkPos;
  const ImVec2 vmax(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
  ImGui::GetBackgroundDrawList()->AddRectFilled(vmin, vmax, u32(t.ink, 0.42f));
  const float split = vmin.x + (vmax.x - vmin.x) * 0.52f;
  beginPane("##pause", ImVec2(split, vmin.y), ImVec2(vmax.x, vmax.y), enter_,
            px(46.0f));
  // The board keeps the other side, so the position sits clear of the menu rather than
  // half behind it.
  request.boardRect[0] = vmin.x;
  request.boardRect[1] = vmin.y;
  request.boardRect[2] = split - vmin.x;
  request.boardRect[3] = vmax.y - vmin.y;
  eyebrow(shell.currentVariant().c_str(), t, mono, scale_);
  screenTitle("Paused", t, display, scale_, 1.8f);
  ImGui::Dummy(ImVec2(0, px(8)));

  const float width = ImGui::GetContentRegionAvail().x;
  if (menuEntry("Continue", 1, t, display, width, true, scale_)) shell.resume();
  if (menuEntry("Game mode", 2, t, display, width, true, scale_)) {
    shell.go(app::Screen::GameInfo);
  }
  if (menuEntry("Piece moves", 3, t, display, width, true, scale_)) {
    shell.go(app::Screen::PieceMoves);
  }
  if (menuEntry("Settings", 4, t, display, width, true, scale_)) {
    shell.go(app::Screen::Settings);
  }
  if (menuEntry("Main menu", 5, t, display, width, true, scale_)) {
    shell.go(app::Screen::MainMenu);
  }
  if (menuEntry("Quit", 6, t, display, width, true, scale_)) request.quit = true;
  endPane();
  return request;
}

UiRequest Ui::buildEditor(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  beginPane("##editor", menuMin, menuMax, enter_, px(46.0f));
  eyebrow("editor", t, mono, scale_);
  screenTitle("Editor", t, display, scale_, 2.0f);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  ImGui::TextWrapped(
      "Boards and pieces are already data - a variant file declares its axes, its "
      "geometry, its pieces and its rules. These editors will write that file for you.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(10)));

  const float width = ImGui::GetContentRegionAvail().x;
  if (menuEntry("Board designer", 1, t, display, width, false, scale_)) {
  }
  if (menuEntry("Piece designer", 2, t, display, width, false, scale_)) {
  }

  ImGui::Dummy(ImVec2(0, px(8)));
  if (button("BACK", t, px(120))) shell.back();
  endPane();
  return request;
}

UiRequest Ui::buildSettings(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);
  app::Settings& s = shell.settings();
  const app::Settings before = s;

  // Opened from the main menu it is a screen of the shell and gets the shell's frame;
  // opened over a game it is a panel on top of the position, which stays visible.
  const bool overBoard = shell.showsBoard();
  ImVec2 menuMin, menuMax;
  if (overBoard) {
    beginPlate("##settings", t, ImVec2(px(600), px(560)), 0.5f);
  } else {
    drawShellFrame(shell, menuMin, menuMax);
    beginPane("##settings", menuMin, menuMax, enter_, px(46.0f));
  }
  eyebrow("settings", t, mono, scale_);
  screenTitle("Settings", t, display, scale_, 2.0f);
  ImGui::Dummy(ImVec2(0, px(4)));

  ImGui::BeginChild("##settingsbody", ImVec2(0, px(430)));
  ImGui::PushItemWidth(px(220));

  heading("DISPLAY", t, small);
  ImGui::SliderFloat("Interface scale", &s.guiScale, 0.6f, 3.0f, "%.2fx");
  if (ImGui::IsItemDeactivatedAfterEdit()) request.applyScale = true;
  ImGui::Checkbox("Fullscreen", &s.fullscreen);
  if (ImGui::IsItemDeactivatedAfterEdit()) request.toggleFullscreen = true;
  ImGui::Checkbox("Wait for vertical sync", &s.vsync);
  ImGui::Checkbox("Flat 2D view", &s.flatView);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("look straight down, pieces as tokens: cheaper to draw");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("LOOKS", t, small);
  {
    // Two sets, and the choice is stored by name so a settings file stays readable and
    // a set added later needs no migration.
    const char* kSets[]{"faceted", "primitive"};
    int current = s.pieceIcons == "primitive" ? 1 : 0;
    if (ImGui::Combo("Piece icons", &current, kSets, 2)) {
      s.pieceIcons = kSets[current];
      request.applyLooks = true;
    }
  }
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("faceted silhouettes, or a handful of flat shapes each");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("BOARD", t, small);
  ImGui::Checkbox("Highlight legal moves", &s.showLegalMoves);
  ImGui::Checkbox("Mark the last move", &s.showLastMove);
  ImGui::Checkbox("Warn when in check", &s.showCheck);
  ImGui::Checkbox("Show seams on glued boards", &s.showSeams);
  ImGui::Checkbox("Show coordinates", &s.showCoordinates);
  ImGui::SliderFloat("Piece height", &s.pieceHeightScale, 0.0f, 2.0f, "%.2f");
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("height encodes value; 0 makes every piece the same");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("CAMERA", t, small);
  ImGui::SliderFloat("Orbit sensitivity", &s.orbitSensitivity, 0.1f, 4.0f, "%.2f");
  ImGui::SliderFloat("Zoom sensitivity", &s.zoomSensitivity, 0.1f, 4.0f, "%.2f");
  ImGui::Checkbox("Invert vertical orbit", &s.invertOrbitY);
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("ANIMATION", t, small);
  ImGui::Checkbox("Animate moves", &s.animateMoves);
  ImGui::BeginDisabled(!s.animateMoves);
  ImGui::SliderFloat("Animation speed", &s.animationSpeed, 0.25f, 4.0f, "%.2fx");
  ImGui::EndDisabled();
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("higher is faster; a wrapping move opens a portal on the edge");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("GAMEPLAY", t, small);
  ImGui::Checkbox("Confirm before moving", &s.confirmMoves);
  {
    // The promotion list belongs to the variant, so the choices are read from it rather
    // than hardcoded; with no game loaded there is nothing to promote to yet.
    std::vector<std::string> options{"always ask"};
    if (const app::Session* session = shell.session(); session != nullptr) {
      const VariantSpec& v = session->variant();
      for (std::size_t i = 1; i < v.pieces.size(); ++i) {
        if (!v.pieces[i].promotesTo.empty()) continue;
        options.push_back(v.pieces[i].name);
      }
    }
    int current = 0;
    for (std::size_t i = 1; i < options.size(); ++i) {
      if (options[i] == s.autoPromoteTo) current = static_cast<int>(i);
    }
    std::vector<const char*> items;
    items.reserve(options.size());
    for (const std::string& o : options) items.push_back(o.c_str());
    if (ImGui::Combo("Promote to", &current, items.data(),
                     static_cast<int>(items.size()))) {
      s.autoPromoteTo =
          current == 0 ? std::string{} : options[static_cast<std::size_t>(current)];
    }
  }
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("AUDIO", t, small);
  ImGui::BeginDisabled();
  ImGui::SliderFloat("Master", &s.volumeMaster, 0.0f, 1.0f, "%.2f");
  ImGui::SliderFloat("Music", &s.volumeMusic, 0.0f, 1.0f, "%.2f");
  ImGui::SliderFloat("Effects", &s.volumeEffects, 0.0f, 1.0f, "%.2f");
  ImGui::EndDisabled();
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("the game has no sound yet; these are kept for when it does");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("CONTROLS", t, small);
  ImGui::PushFont(small);
  keyValue("select / move", "left click", t);
  keyValue("orbit", "right drag", t);
  keyValue("zoom", "wheel", t);
  keyValue("pause", "Esc", t);
  keyValue("undo", "U", t);
  keyValue("reset", "R", t);
  ImGui::PopFont();

  ImGui::PopItemWidth();
  ImGui::EndChild();

  ImGui::Dummy(ImVec2(0, px(4)));
  if (button("BACK", t, px(120), true)) shell.back();
  ImGui::SameLine();
  if (button("RESET TO DEFAULTS", t, px(200))) {
    const std::string keepVariant = s.lastVariant;
    s = app::Settings{};
    s.lastVariant = keepVariant;
    request.applyScale = true;
    request.settingsChanged = true;
  }
  ImGui::SameLine();
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted("saved as you change them");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  if (overBoard) {
    endPlate();
  } else {
    endPane();
  }

  // Persist whenever anything actually moved, rather than on a Save button nobody
  // should have to find.
  // A struct with a std::string in it cannot be compared byte-wise; compare the
  // fields that a control can actually change.
  const bool moved =
      before.guiScale != s.guiScale || before.fullscreen != s.fullscreen ||
      before.vsync != s.vsync || before.showLegalMoves != s.showLegalMoves ||
      before.showLastMove != s.showLastMove || before.showCheck != s.showCheck ||
      before.showSeams != s.showSeams || before.showCoordinates != s.showCoordinates ||
      before.pieceHeightScale != s.pieceHeightScale || before.flatView != s.flatView ||
      before.pieceIcons != s.pieceIcons ||
      before.animateMoves != s.animateMoves ||
      before.animationSpeed != s.animationSpeed ||
      before.orbitSensitivity != s.orbitSensitivity ||
      before.zoomSensitivity != s.zoomSensitivity ||
      before.invertOrbitY != s.invertOrbitY || before.confirmMoves != s.confirmMoves ||
      before.autoPromoteTo != s.autoPromoteTo || before.volumeMaster != s.volumeMaster ||
      before.volumeMusic != s.volumeMusic || before.volumeEffects != s.volumeEffects;
  if (moved) request.settingsChanged = true;
  return request;
}

UiRequest Ui::buildGameInfo(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);
  const app::Session* session = shell.session();
  if (session == nullptr) {
    shell.back();
    return request;
  }
  const VariantSpec& v = session->variant();

  beginPlate("##gameinfo", t, ImVec2(px(620), px(520)), 0.5f);
  eyebrow("game mode", t, mono, scale_);
  screenTitle(v.name.c_str(), t, display, scale_, 2.0f);
  ImGui::Dummy(ImVec2(0, px(4)));

  ImGui::BeginChild("##infobody", ImVec2(0, px(400)));
  heading("BOARD", t, small);
  ImGui::PushFont(small);
  for (std::uint8_t a = 0; a < v.dims.dims(); ++a) {
    keyValue(v.dims.name(a).c_str(), std::to_string(v.dims.extent(a)) + " cells", t);
  }
  keyValue("total cells", std::to_string(v.dims.cellCount()), t);
  keyValue("geometry",
           v.geom.isBox() ? "a plain box"
                          : (v.geom.isOrientable() ? "glued" : "glued with a twist"),
           t, !v.geom.isBox());
  if (!v.geom.isBox()) {
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.rift));
    ImGui::TextWrapped(
        v.geom.isOrientable()
            ? "Edges of this board are joined to each other, so a piece leaving one side "
              "arrives at another. Cyan edges mark where."
            : "This board is joined to itself with a twist, so it has no consistent "
              "sense "
              "of up. A bishop here is not confined to one colour.");
    ImGui::PopStyleColor();
  }
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(8)));

  heading("RULES", t, small);
  ImGui::PushFont(small);
  keyValue("en passant", v.enPassant ? "yes" : "no", t);
  keyValue("castling", v.castles.empty() ? "no" : "yes", t);
  const auto& promo = v.promotion[static_cast<std::size_t>(Color::White)];
  keyValue("promotion", promo.active() ? "on the far rank" : "none on this board", t);
  keyValue("stalemate",
           v.stalemate == StalematePolicy::Draw
               ? "a draw"
               : (v.stalemate == StalematePolicy::Loss ? "a loss" : "a win"),
           t);
  keyValue("draw clock",
           v.halfmoveDrawLimit > 0 ? std::to_string(v.halfmoveDrawLimit) + " half-moves"
                                   : "off",
           t);
  ImGui::PopFont();

  if (v.ruleSet) {
    ImGui::Dummy(ImVec2(0, px(8)));
    heading("SPECIAL RULES", t, small);
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    ImGui::TextWrapped("%s", session->game().ruleEngine().ruleSet().describe().c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::EndChild();

  ImGui::Dummy(ImVec2(0, px(4)));
  if (button("BACK", t, px(120), true)) shell.back();
  endPlate();
  return request;
}

UiRequest Ui::buildPieceMoves(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);
  const app::Session* session = shell.session();
  if (session == nullptr) {
    shell.back();
    return request;
  }
  const VariantSpec& v = session->variant();

  beginPlate("##pieces", t, ImVec2(px(700), px(560)), 0.5f);
  eyebrow("reference", t, mono, scale_);
  screenTitle("Piece moves", t, display, scale_, 1.9f);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::TextUnformatted(
      "filled dots are reached by sliding, rings by leaping over whatever is between");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(6)));

  ImGui::BeginChild("##piecebody", ImVec2(0, px(430)));
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    const PieceTypeDef& piece = v.pieces[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::BeginGroup();
    drawMoveDiagram(v, piece, t, px(15.0f));
    ImGui::EndGroup();
    ImGui::SameLine();

    ImGui::BeginGroup();
    ImGui::PushFont(display);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.bone));
    ImGui::Text("%s", upper(piece.name).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::Text("symbol %c%s%s", piece.symbol, piece.royal ? "  -  royal" : "",
                piece.promotesTo.empty() ? "" : "  -  promotes");
    ImGui::Text("drawn as a %s", std::string(archetypeName(archetypeFor(piece))).c_str());
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    for (const MoveAtom& atom : piece.atoms) {
      const std::uint32_t count = atom.dirCount(Color::White);
      ImGui::Text("%s  ->  %u direction%s", atom.toString().c_str(), count,
                  count == 1 ? "" : "s");
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndGroup();

    ImGui::PopID();
    ImGui::Dummy(ImVec2(0, px(6)));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, px(2)));
  }
  ImGui::EndChild();

  ImGui::Dummy(ImVec2(0, px(4)));
  if (button("BACK", t, px(120), true)) shell.back();
  endPlate();
  return request;
}

}  // namespace cb::render
