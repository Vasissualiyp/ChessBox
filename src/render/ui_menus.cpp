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
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "io/notation.hpp"
#include "render/piece_mesh.hpp"
#include "render/ui.hpp"
#include "render/ui_widgets.hpp"

namespace cb::render {
namespace {

using namespace widgets;

/// The colour a difficulty is advertised with. A switch, not an array lookup, so that
/// adding a level to the enum is a compile error here rather than a silent wrap.
view::Rgba difficultyColor(const view::Theme& t, Difficulty d) {
  switch (d) {
    case Difficulty::Easy:
      return t.diffEasy;
    case Difficulty::Medium:
      return t.diffMedium;
    case Difficulty::Hard:
      return t.diffHard;
    case Difficulty::Impossible:
      return t.diffImpossible;
    case Difficulty::Other:
      return t.diffOther;
  }
  return t.diffOther;
}

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

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##mainmenu", menuMin, menuMax, move.scale, move.alpha, px(46.0f),
            !ghosting_);
  screenTitle("CHESSBOX", t, display, scale_, 2.4f);
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
  if (menuEntry("Quit", 6, t, display, width, true, scale_)) {
    shell.go(app::Screen::QuitConfirm);
  }

  endPane();
  return request;
}

UiRequest Ui::buildQuitConfirm(app::Shell& shell) {
  UiRequest request;
  const view::Theme& t = theme_;
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);
  auto* mono = static_cast<ImFont*>(fontMono_);

  ImVec2 menuMin, menuMax;
  drawShellFrame(shell, menuMin, menuMax);
  const PaneMove move = paneMove();
  beginPane("##quitconfirm", menuMin, menuMax, move.scale, move.alpha, px(46.0f),
            !ghosting_);
  eyebrow("quit", t, mono, scale_);
  screenTitle("Are you sure?", t, display, scale_, 1.9f);
  ImGui::Dummy(ImVec2(0, px(6)));
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
  ImGui::TextWrapped(
      "The window will close. A position that has not been saved is lost.");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(14)));

  // Leaving is the destructive choice, so it is not the one under the pointer by
  // default and it is not the accent: backing out is.
  if (button("BACK", t, px(150), true, false, true, display)) shell.back();
  ImGui::SameLine();
  if (button("QUIT", t, px(110), false, false, true, display)) request.quit = true;
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
  const PaneMove move = paneMove();
  beginPane("##newgame", menuMin, menuMax, move.scale, move.alpha, px(46.0f), !ghosting_);
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
    // The difficulty is a property of the variant, resolved once by the shell; the
    // colour follows from it. The selected row is filled with its own difficulty
    // colour - the choice states how hard the thing you picked is - and every row
    // carries a small tab of that colour at its left, so the level reads before a
    // click. `bone` is the type colour that contrasts with the page in both themes,
    // which is what keeps the text legible on the saturated fill.
    const view::Rgba diff = difficultyColor(t, shell.difficultyOf(name));
    ImGui::PushID(name.c_str());
    const float rowW = ImGui::GetContentRegionAvail().x;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + rowW, min.y + rowH);
    const bool clicked = ImGui::InvisibleButton("##row", ImVec2(rowW, rowH));
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
      dl->AddRectFilled(min, max, u32(diff, 0.88f), px(4));
    } else if (hovered) {
      dl->AddRectFilled(min, max, u32(t.panelHi), px(4));
    }
    const float tabW = px(5.0f);
    dl->AddRectFilled(ImVec2(min.x, min.y + px(3)), ImVec2(min.x + tabW, max.y - px(3)),
                      u32(diff, active ? 1.0f : 0.78f), px(2));
    const ImU32 textCol = u32(active ? t.bone : (hovered ? t.bone : t.boneDim));
    dl->AddText(
        ImVec2(min.x + px(12) + tabW, min.y + (rowH - ImGui::GetFontSize()) * 0.5f),
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
  if (button("START", t, px(150), true, false, true, display)) {
    request.loadVariant = pickedVariant_;
  }
  ImGui::SameLine();
  if (button("BACK", t, px(110), false, false, true, display)) shell.back();
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
  const PaneMove move = paneMove();
  beginPane("##pause", ImVec2(split, vmin.y), ImVec2(vmax.x, vmax.y), move.scale,
            move.alpha, px(46.0f), !ghosting_);
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
  if (menuEntry("Quit", 6, t, display, width, true, scale_)) {
    shell.go(app::Screen::QuitConfirm);
  }
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
  const PaneMove move = paneMove();
  beginPane("##editor", menuMin, menuMax, move.scale, move.alpha, px(46.0f), !ghosting_);
  eyebrow("editor", t, mono, scale_);
  screenTitle("Piece designer", t, display, scale_, 1.9f);

  app::Editor* editor = shell.editor();
  if (editor == nullptr) {
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    ImGui::TextWrapped("No variant is open to edit. Start one, then return here.");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, px(10)));
    if (button("BACK", t, px(120), false, false, true, display)) shell.back();
    endPane();
    return request;
  }

  const auto magsText = [](const MoveAtom& a) {
    std::string s = "[";
    for (std::size_t i = 0; i < a.mags.size(); ++i) {
      if (i != 0) s += ",";
      s += std::to_string(a.mags[i]);
    }
    s += "]";
    return s;
  };
  const auto parseMags = [](const char* text, SmallVec<std::int16_t, kMaxDims>& out) {
    SmallVec<std::int16_t, kMaxDims> m;
    const char* p = text;
    while (*p != '\0') {
      while (*p == ' ' || *p == ',' || *p == '[' || *p == ']' || *p == '\t') ++p;
      if (*p == '\0') break;
      char* end = nullptr;
      const long v = std::strtol(p, &end, 10);
      if (end == p || v <= 0 || v > 32767 || m.size() >= kMaxDims) return false;
      m.push(static_cast<std::int16_t>(v));
      p = end;
    }
    if (m.empty()) return false;
    out = m;
    return true;
  };

  // ---- header: what is open, whether it is saved, and the edit controls.
  ImGui::PushFont(mono);
  ImGui::PushStyleColor(ImGuiCol_Text, col(editor->dirty() ? t.ember : t.boneDim));
  ImGui::Text("%s%s", shell.editorVariant().c_str(), editor->dirty() ? "  *unsaved" : "");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(0, px(4)));
  if (button("UNDO", t, px(84), false, true, false, display)) editor->undo();
  ImGui::SameLine();
  if (button("REDO", t, px(84), false, true, false, display)) editor->redo();
  ImGui::SameLine();
  if (button("SAVE", t, px(84), editor->dirty(), false, false, display)) {
    (void)shell.saveEditor();
  }
  ImGui::Dummy(ImVec2(0, px(8)));

  const std::vector<std::string> names = editor->pieceNames();
  if (names.empty()) {
    ImGui::TextUnformatted("This variant declares no pieces.");
  } else if (ImGui::BeginTable(
                 "##editorcols", 2,
                 ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
    if (editorPiece_ < 0 || editorPiece_ >= static_cast<int>(names.size()))
      editorPiece_ = 0;
    ImGui::TableSetupColumn("pieces", ImGuiTableColumnFlags_WidthFixed, px(120.0f));
    ImGui::TableSetupColumn("moves", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();

    ImGui::TableSetColumnIndex(0);
    ImGui::PushFont(small);
    for (int i = 0; i < static_cast<int>(names.size()); ++i) {
      const bool selected = i == editorPiece_;
      ImGui::PushStyleColor(ImGuiCol_Text, col(selected ? t.ember : t.boneDim));
      if (ImGui::Selectable(names[static_cast<std::size_t>(i)].c_str(), selected)) {
        editorPiece_ = i;
      }
      ImGui::PopStyleColor();
    }
    ImGui::PopFont();

    ImGui::TableSetColumnIndex(1);
    const std::string piece = names[static_cast<std::size_t>(editorPiece_)];
    auto atoms = editor->pieceAtoms(piece);
    if (!atoms.has_value()) {
      ImGui::TextUnformatted("this piece could not be read");
    } else {
      std::vector<MoveAtom> list = *atoms;
      bool changed = false;
      std::size_t removeAt = list.size();
      ImGui::PushFont(small);
      for (std::size_t i = 0; i < list.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::Text("%2zu", i + 1);
        ImGui::SameLine();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", magsText(list[i]).c_str());
        ImGui::SetNextItemWidth(px(96));
        if (ImGui::InputText("##vec", buf, sizeof(buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
          if (parseMags(buf, list[i].mags)) changed = true;
        }
        int mode = list[i].mode == MoveMode::Slide
                       ? 0
                       : (list[i].mode == MoveMode::Leap ? 1 : 2);
        ImGui::SetNextItemWidth(px(76));
        if (ImGui::Combo("##mode", &mode, "slide\0leap\0hop\0")) {
          list[i].mode = mode == 0   ? MoveMode::Slide
                         : mode == 1 ? MoveMode::Leap
                                     : MoveMode::Hop;
          changed = true;
        }
        ImGui::SameLine();
        int cap = list[i].capture == CapturePolicy::May
                      ? 0
                      : (list[i].capture == CapturePolicy::Must ? 1 : 2);
        ImGui::SetNextItemWidth(px(84));
        if (ImGui::Combo("##cap", &cap, "may\0must\0cannot\0")) {
          list[i].capture = cap == 0   ? CapturePolicy::May
                            : cap == 1 ? CapturePolicy::Must
                                       : CapturePolicy::Cannot;
          changed = true;
        }
        ImGui::SameLine();
        bool rider = list[i].maxK == kUnlimited;
        if (ImGui::Checkbox("ride", &rider)) {
          list[i].maxK = rider ? kUnlimited : 1;
          changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("fwd", &list[i].oriented)) changed = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) removeAt = i;
        ImGui::PopID();
      }
      ImGui::PopFont();
      if (removeAt < list.size()) {
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(removeAt));
        changed = true;
      }

      // The composition palette: dropping a bundle in unions its atoms, which is how
      // rook + bishop becomes a queen in 2-D and something else entirely above it.
      ImGui::Dummy(ImVec2(0, px(6)));
      const auto addAtom = [&](std::initializer_list<std::int16_t> mags,
                               std::uint32_t maxK, MoveMode m, CapturePolicy c,
                               bool fwd) {
        MoveAtom a;
        for (std::int16_t v : mags) a.mags.push(v);
        a.maxK = maxK;
        a.mode = m;
        a.capture = c;
        a.oriented = fwd;
        auto canon = MoveAtom::canonicalize(a);
        if (canon.has_value()) list.push_back(*canon);
        changed = true;
      };
      if (ImGui::SmallButton("+ rook [1]inf")) {
        addAtom({1}, kUnlimited, MoveMode::Slide, CapturePolicy::May, false);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+ bishop [1,1]inf")) {
        addAtom({1, 1}, kUnlimited, MoveMode::Slide, CapturePolicy::May, false);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+ knight [1,2]")) {
        addAtom({1, 2}, 1, MoveMode::Leap, CapturePolicy::May, false);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+ king step [1]")) {
        addAtom({1}, 1, MoveMode::Leap, CapturePolicy::May, false);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+ diag [1,1]")) {
        addAtom({1, 1}, 1, MoveMode::Leap, CapturePolicy::May, false);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+ pawn push fwd")) {
        addAtom({1}, 1, MoveMode::Leap, CapturePolicy::Cannot, true);
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("+ pawn capture fwd")) {
        addAtom({1, 1}, 1, MoveMode::Leap, CapturePolicy::Must, true);
      }

      if (changed) {
        bool ok = true;
        for (MoveAtom& a : list) {
          auto canon = MoveAtom::canonicalize(a);
          if (!canon.has_value()) {
            ok = false;
            break;
          }
          a = *canon;
        }
        if (ok) (void)editor->setPieceAtoms(piece, list);
      }
    }
    ImGui::EndTable();
  }

  ImGui::Dummy(ImVec2(0, px(10)));
  if (button("BACK", t, px(120), false, false, true, display)) shell.back();
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
    const PaneMove move = paneMove();
    beginPane("##settings", menuMin, menuMax, move.scale, move.alpha, px(46.0f),
              !ghosting_);
  }
  eyebrow("settings", t, mono, scale_);
  screenTitle("Settings", t, display, scale_, 2.0f);
  ImGui::Dummy(ImVec2(0, px(4)));

  // The body is the one scroller; the panel itself never scrolls, and the height leaves
  // room for the Back button beneath it so it can never be clipped or hidden.
  ImGui::BeginChild(
      "##settingsbody",
      ImVec2(0, std::max(px(120.0f), ImGui::GetContentRegionAvail().y - px(44.0f))));
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
  if (button("BACK", t, px(120), true, false, true, display)) shell.back();
  ImGui::SameLine();
  if (button("RESET TO DEFAULTS", t, px(200), false, false, true, display)) {
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
      before.pieceIcons != s.pieceIcons || before.animateMoves != s.animateMoves ||
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

  ImGui::BeginChild(
      "##infobody",
      ImVec2(0, std::max(px(120.0f), ImGui::GetContentRegionAvail().y - px(44.0f))));
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
  if (button("BACK", t, px(120), true, false, true, display)) shell.back();
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

  ImGui::BeginChild(
      "##piecebody",
      ImVec2(0, std::max(px(120.0f), ImGui::GetContentRegionAvail().y - px(44.0f))));
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
  if (button("BACK", t, px(120), true, false, true, display)) shell.back();
  endPlate();
  return request;
}

}  // namespace cb::render
