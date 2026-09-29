// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ui.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "io/notation.hpp"
#include "render/offscreen_target.hpp"
#include "render/deco.hpp"
#include "render/piece_icon.hpp"
#include "render/piece_mesh.hpp"
#include "render/ui_widgets.hpp"

namespace cb::render {
using namespace widgets;
namespace {

std::filesystem::path findFont(const char* name) {
  // Relative to the *binary*, not the working directory: the game must look the same
  // whether it is launched from the build tree, from an install, or from a menu entry
  // with no sensible cwd at all.
  std::vector<std::filesystem::path> roots;
  if (const char* base = SDL_GetBasePath(); base != nullptr) {
    const std::filesystem::path exe(base);
    roots.push_back(exe / "fonts");
    roots.push_back(exe / ".." / "fonts");
    roots.push_back(exe / ".." / ".." / "fonts");
    roots.push_back(exe / ".." / "share" / "chessbox" / "fonts");
  }
  for (const char* rel : {"fonts", "../fonts", "build/dev/fonts", "share/chessbox/fonts",
                          "/usr/share/chessbox/fonts"}) {
    roots.emplace_back(rel);
  }
  for (const auto& root : roots) {
    std::error_code ec;
    const std::filesystem::path p = root / name;
    if (std::filesystem::exists(p, ec)) return p;
  }
  return {};
}

/// A heading with a rule running to the right of it, the way a plate is labelled.

/// A piece drawn as a flat icon, for the top-down view where a model would be a blob.
///
/// The outlines come from a table rather than from a switch full of drawing calls, so
/// the two styles differ only in which table is read, a variant could eventually ship
/// its own, and the shapes themselves are covered by tests that need no GPU.
///
/// `behind` is what the icon is drawn on: the cut-outs are painted in it, which is how
/// a knight gets an eye out of a single-colour silhouette.
void drawPieceGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 behind,
                    Archetype shape, IconStyle style) {
  const PieceIcon icon = pieceIcon(style, shape);
  // The box is 100 wide; r is the icon's half-size on screen.
  const float k = r / 50.0f;
  const auto trace = [&](const IconPoly& poly, ImU32 fill) {
    if (poly.size() < 3) return;
    dl->PathClear();
    for (const IconPoint& p : poly) {
      dl->PathLineTo(ImVec2(c.x + (p.x - 50.0f) * k, c.y + (p.y - 50.0f) * k));
    }
    // Concave: a rook's crenellations and a queen's points are not convex hulls, and
    // the convex filler turns them into blocks.
    dl->PathFillConcave(fill);
  };
  for (const IconPoly& poly : icon.fills) trace(poly, col);
  for (const IconPoly& poly : icon.cuts) trace(poly, behind);
}

}  // namespace

Result<std::unique_ptr<Ui>> Ui::create(const VulkanContext& ctx, SDL_Window* window,
                                       const view::Theme& theme) {
  auto ui = std::unique_ptr<Ui>(new Ui());
  ui->ctx_ = &ctx;
  ui->window_ = window;
  ui->theme_ = theme;

  // ImGui needs its own descriptor pool; one combined-image-sampler set per font atlas
  // and per user texture is plenty for an interface with no images in it.
  const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8}};
  VkDescriptorPoolCreateInfo dpci{};
  dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  dpci.maxSets = 8;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = sizes;
  if (const VkResult r = vkCreateDescriptorPool(ctx.device(), &dpci, nullptr, &ui->pool_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the interface's descriptor pool: " + describe(r));
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // no stray imgui.ini beside the binary
  // Keyboard navigation draws a focus ring that is larger than the item it surrounds,
  // which makes a menu look like its rows are different heights. The game is driven by
  // the mouse, so it is off until the ring can be styled to match the rest.
  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

  if (auto r = ui->loadFonts(); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  ui->applyStyle();

  if (!ImGui_ImplSDL3_InitForVulkan(window)) {
    return fail(ErrorCode::Unsupported, "cannot attach the interface to the window");
  }

  const VkFormat colorFormat = OffscreenTarget::kColorFormat;
  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = OffscreenTarget::kDepthFormat;

  ImGui_ImplVulkan_InitInfo info{};
  info.Instance = ctx.instance();
  info.PhysicalDevice = ctx.physicalDevice();
  info.Device = ctx.device();
  info.QueueFamily = ctx.queueFamily();
  info.Queue = ctx.queue();
  info.DescriptorPool = ui->pool_;
  info.MinImageCount = 2;
  info.ImageCount = 2;
  info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  // The interface is drawn into the same pass as the board, so it declares the same
  // attachment formats. One render path, not two.
  info.UseDynamicRendering = true;
  info.PipelineRenderingCreateInfo = rendering;
  if (!ImGui_ImplVulkan_Init(&info)) {
    return fail(ErrorCode::Unsupported, "cannot initialise the interface renderer");
  }
  ui->initialised_ = true;
  return ui;
}

Result<void> Ui::loadFonts() {
  ImGuiIO& io = ImGui::GetIO();
  // Three voices, three jobs: Chakra Petch names things, Public Sans explains them, and
  // JetBrains Mono is reserved for anything the engine computed. That last rule is what
  // makes a number look like a measurement rather than like prose.
  const auto display = findFont("ChakraPetch-Bold.ttf");
  const auto body = findFont("PublicSans-Regular.ttf");
  const auto light = findFont("PublicSans-Light.ttf");
  const auto mono = findFont("JetBrainsMono-Regular.ttf");

  const auto load = [&](const std::filesystem::path& path, float size,
                        float tracking = 0.0f) -> void* {
    if (path.empty()) return nullptr;
    ImFontConfig cfg;
    cfg.GlyphExtraSpacing.x = tracking * scale_;
    return io.Fonts->AddFontFromFileTTF(path.string().c_str(), size * scale_, &cfg);
  };

  fontBody_ = load(body, 15.5f);
  if (fontBody_ == nullptr) fontBody_ = io.Fonts->AddFontDefault();
  fontSmall_ = load(light.empty() ? body : light, 12.5f);
  if (fontSmall_ == nullptr) fontSmall_ = fontBody_;
  fontMono_ = load(mono, 12.0f, 0.2f);
  if (fontMono_ == nullptr) fontMono_ = fontSmall_;
  // Tracked out a little: Chakra Petch is set in caps here, and caps need the air.
  fontDisplay_ = load(display, 17.0f, 0.9f);
  // A missing typeface must not stop the game starting; it just looks plainer.
  if (fontDisplay_ == nullptr) fontDisplay_ = fontBody_;

  io.FontDefault = static_cast<ImFont*>(fontBody_);
  return {};
}

void Ui::applyStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  // Start from stock so re-applying after a rescale is idempotent rather than cumulative.
  s = ImGuiStyle{};
  // Machined edges. Rounding is the single thing that makes ImGui look like a debug
  // overlay, so the panels get none and the controls get just enough to look milled.
  s.WindowRounding = 0.0f;
  s.ChildRounding = 6.0f;
  s.FrameRounding = 2.0f;
  s.PopupRounding = 0.0f;
  s.ScrollbarRounding = 0.0f;
  s.GrabRounding = 2.0f;
  // The plate draws its own thick outline; letting ImGui add a second, thin one on top
  // of it just muddies the edge.
  s.WindowBorderSize = 0.0f;
  s.ChildBorderSize = 1.0f;
  s.FrameBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  s.WindowPadding = ImVec2(14, 14);
  s.FramePadding = ImVec2(9, 5);
  s.ItemSpacing = ImVec2(8, 8);
  s.ItemInnerSpacing = ImVec2(6, 5);
  s.IndentSpacing = 14.0f;
  s.ScrollbarSize = 10.0f;

  const view::Theme& t = theme_;
  ImVec4* c = s.Colors;
  c[ImGuiCol_WindowBg] = col(t.soot, 0.96f);
  c[ImGuiCol_ChildBg] = col(t.panel, 0.0f);
  c[ImGuiCol_PopupBg] = col(t.panel, 0.99f);
  c[ImGuiCol_Border] = col(t.rule);
  c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_FrameBg] = col(t.panel);
  c[ImGuiCol_FrameBgHovered] = col(t.panelHi);
  c[ImGuiCol_FrameBgActive] = col(t.panelHi);
  c[ImGuiCol_TitleBg] = col(t.panel);
  c[ImGuiCol_TitleBgActive] = col(t.panelHi);
  c[ImGuiCol_Text] = col(t.bone);
  c[ImGuiCol_TextDisabled] = col(t.boneFaint);
  c[ImGuiCol_Button] = col(t.panel);
  c[ImGuiCol_ButtonHovered] = col(t.panelHi);
  // A press lands on the accent, so a click feels like it did something.
  c[ImGuiCol_ButtonActive] = col(t.ember);
  c[ImGuiCol_Header] = col(t.panelHi);
  c[ImGuiCol_HeaderHovered] = col(t.panelHi);
  c[ImGuiCol_HeaderActive] = col(t.emberDeep);
  c[ImGuiCol_Separator] = col(t.rule);
  c[ImGuiCol_ScrollbarBg] = col(t.ink, 0.6f);
  c[ImGuiCol_ScrollbarGrab] = col(t.rule);
  c[ImGuiCol_ScrollbarGrabHovered] = col(t.emberDeep);
  c[ImGuiCol_ScrollbarGrabActive] = col(t.ember);
  c[ImGuiCol_CheckMark] = col(t.ember);
  c[ImGuiCol_SliderGrab] = col(t.ember);
  c[ImGuiCol_SliderGrabActive] = col(t.emberDeep);
  c[ImGuiCol_TextSelectedBg] = col(t.emberDeep, 0.5f);
  c[ImGuiCol_ModalWindowDimBg] = col(t.ink, 0.72f);

  // Sizes scale with the interface; colours do not.
  s.ScaleAllSizes(scale_);
}

Result<void> Ui::setScale(float scale) {
  const float clamped = std::clamp(scale, 0.6f, 3.0f);
  if (std::abs(clamped - scale_) < 0.01f) return {};
  scale_ = clamped;

  // Fonts are rebuilt at the new size rather than stretched - a scale option that makes
  // text blurry is not worth having - and that means waiting for the GPU to finish with
  // the old atlas first.
  vkDeviceWaitIdle(ctx_->device());
  ImGui_ImplVulkan_DestroyFontsTexture();
  ImGui::GetIO().Fonts->Clear();
  fontBody_ = nullptr;
  fontSmall_ = nullptr;
  fontDisplay_ = nullptr;
  if (auto r = loadFonts(); !r.has_value()) return r;
  if (!ImGui_ImplVulkan_CreateFontsTexture()) {
    return fail(ErrorCode::Internal, "cannot rebuild the interface font atlas");
  }
  applyStyle();
  return {};
}

bool Ui::processEvent(const SDL_Event& e) {
  ImGui_ImplSDL3_ProcessEvent(&e);
  const ImGuiIO& io = ImGui::GetIO();
  switch (e.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
      return io.WantCaptureMouse;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
      return io.WantCaptureKeyboard;
    default:
      return false;
  }
}

bool Ui::capturesMouse() const {
  return ImGui::GetIO().WantCaptureMouse;
}
bool Ui::capturesKeyboard() const {
  return ImGui::GetIO().WantCaptureKeyboard;
}

void Ui::tick(float dt) {
  clock_ += dt;
  field_.advance(dt);
  // Ease the arrival rather than run it linearly: a screen that stops dead has not
  // travelled anywhere.
  if (enter_ < 1.0f) enter_ = std::min(1.0f, enter_ + dt * 2.2f);
}

void Ui::newFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void Ui::drawShellFrame(app::Shell& shell, ImVec2& menuMin, ImVec2& menuMax) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const ImVec2 min = vp->WorkPos;
  const ImVec2 max = ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);

  // The ground. A light shell has to paint its own, or the board's clear colour shows
  // through where nothing else is drawn.
  dl->AddRectFilled(min, max, u32(theme_.ink));
  field_.draw(dl, min, max, theme_, iconStyle_, true, true);

  // Deeper screens put their object on the other side. Alternating by depth is what
  // makes each level a distinct place rather than a fade, and it needs no per-screen
  // authoring: it falls out of the depth alone.
  const bool decoLeft = app::screenDepth(shell.screen()) % 2 == 0;
  const float split = (min.x + max.x) * 0.5f;
  const ImVec2 decoMin(decoLeft ? min.x : split, min.y);
  const ImVec2 decoMax(decoLeft ? split : max.x, max.y);
  menuMin = ImVec2(decoLeft ? split : min.x, min.y);
  menuMax = ImVec2(decoLeft ? max.x : split, max.y);

  // The library's lattice is the *selected* variant's, not the running game's - the
  // whole point of it is to show you the shape of the board you are about to choose.
  const VariantSpec* subject =
      shell.screen() == app::Screen::NewGame
          ? shell.preview(pickedVariant_)
          : (shell.hasGame() ? &shell.session()->variant() : nullptr);
  drawDeco(dl, decoForScreen(static_cast<int>(shell.screen())), decoMin, decoMax, theme_,
           iconStyle_, clock_, subject);

  // A hairline between the object and the menu, and the depth ladder on the far left.
  dl->AddLine(ImVec2(split, min.y), ImVec2(split, max.y), u32(theme_.rule, 0.6f));

  // The ladder lives on the decoration side's outer edge. The menu side is full of
  // list rows and buttons that reach the margins; the object is centred in its own half
  // and leaves the edge clear.
  const int depth = app::screenDepth(shell.screen());
  const char* rungs[]{"shell", "choose", "play"};
  auto* small = static_cast<ImFont*>(fontMono_);
  const float ladderX = decoLeft ? min.x + px(20.0f) : max.x - px(80.0f);
  for (int i = 0; i < 3; ++i) {
    const float y = max.y - px(96.0f) + static_cast<float>(i) * px(20.0f);
    const bool on = i <= depth;
    const ImVec2 c(ladderX, y);
    const float r = px(3.5f);
    const ImVec2 quad[4]{ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r),
                         ImVec2(c.x - r, c.y)};
    if (on) {
      dl->AddConvexPolyFilled(quad, 4, u32(theme_.ember));
    } else {
      dl->AddPolyline(quad, 4, u32(theme_.rule), ImDrawFlags_Closed, 1.0f);
    }
    if (small != nullptr) {
      dl->AddText(small, small->FontSize, ImVec2(c.x + px(10.0f), y - px(6.0f)),
                  u32(on ? theme_.boneDim : theme_.rule), rungs[i]);
    }
  }
}

UiRequest Ui::build(app::Shell& shell, float fps) {
  // A screen change starts the camera moving and shoves the field towards the viewer -
  // or away from it, on the way back out.
  const int screen = static_cast<int>(shell.screen());
  if (screen != lastScreen_) {
    const bool deeper = lastScreen_ < 0 || app::screenDepth(shell.screen()) >=
                                               app::screenDepth(
                                                   static_cast<app::Screen>(lastScreen_));
    field_.push(deeper ? 3.1f : -2.6f);
    enter_ = 0.0f;
    lastScreen_ = screen;
  }

  switch (shell.screen()) {
    case app::Screen::MainMenu:
      return buildMainMenu(shell);
    case app::Screen::NewGame:
      return buildNewGame(shell);
    case app::Screen::Editor:
      return buildEditor(shell);
    case app::Screen::Settings: {
      // Opened over a game, the rails stay up so the position behind stays readable.
      UiRequest out;
      if (shell.showsBoard()) out = buildGameHud(shell, fps);
      const UiRequest settings = buildSettings(shell);
      out.settingsChanged = settings.settingsChanged;
      out.applyScale = settings.applyScale;
      out.toggleFullscreen = settings.toggleFullscreen;
      out.quit = out.quit || settings.quit;
      return out;
    }
    case app::Screen::GameInfo: {
      UiRequest out = buildGameHud(shell, fps);
      const UiRequest info = buildGameInfo(shell);
      out.quit = out.quit || info.quit;
      return out;
    }
    case app::Screen::PieceMoves: {
      UiRequest out = buildGameHud(shell, fps);
      const UiRequest moves = buildPieceMoves(shell);
      out.quit = out.quit || moves.quit;
      return out;
    }
    case app::Screen::Paused: {
      // No heads-up display behind the pause menu. The readouts are for a player who is
      // moving; a paused board wants the position visible and nothing else competing
      // with the menu for the corners.
      return buildPause(shell);
    }
    case app::Screen::Game:
      break;
  }
  return buildGameHud(shell, fps);
}

UiRequest Ui::buildGameHud(app::Shell& shell, float fps) {
  UiRequest request;
  const view::Theme& t = theme_;
  app::Session& session = *shell.session();
  const VariantSpec& v = session.variant();
  const Game& game = session.game();
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  // No rails while playing. The board is the thing; everything else sits in a corner and
  // gets out of the way.
  request.boardRect[0] = 0.0f;
  request.boardRect[1] = 0.0f;
  request.boardRect[2] = vp->Size.x;
  request.boardRect[3] = vp->Size.y;

  const ImGuiWindowFlags overlayFlags =
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

  const float margin = px(22.0f);

  // ---------------------------------------------------------- variant, top left
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + margin, vp->WorkPos.y + margin));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##variant", nullptr, overlayFlags);
  {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::PushFont(display);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
    ImGui::TextUnformatted(upper(shell.currentVariant()).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    // A hairline under the name, struck like an engraved plate.
    const ImVec2 nameMax = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(origin.x, nameMax.y + px(3)),
                                        ImVec2(nameMax.x, nameMax.y + px(3)),
                                        u32(t.emberDeep), 1.0f);
    ImGui::Dummy(ImVec2(0, px(5)));
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(v.geom.isBox() ? t.boneFaint : t.rift));
    ImGui::TextUnformatted(
        v.geom.isBox() ? "flat board"
                       : (v.geom.isOrientable() ? "glued board" : "glued with a twist"));
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::End();
  ImGui::PopStyleVar();

  // ------------------------------------------------------------ status, top right
  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin, vp->WorkPos.y + margin),
      ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##status", nullptr, overlayFlags);
  {
    const GameResult result = game.result();
    const auto rightAlign = [&](const char* text, ImFont* font,
                                const view::Rgba& colour) {
      if (font != nullptr) ImGui::PushFont(font);
      const float width = ImGui::CalcTextSize(text).x;
      const float avail = ImGui::GetContentRegionAvail().x;
      if (avail > width) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
      ImGui::PushStyleColor(ImGuiCol_Text, col(colour));
      ImGui::TextUnformatted(text);
      ImGui::PopStyleColor();
      if (font != nullptr) ImGui::PopFont();
    };

    if (result == GameResult::InProgress) {
      const bool white = game.position().sideToMove() == Color::White;
      rightAlign(white ? "WHITE TO MOVE" : "BLACK TO MOVE", display, t.ember);
    } else {
      rightAlign(upper(std::string(toString(result))).c_str(), display, t.blood);
    }

    char line[128];
    std::snprintf(line, sizeof(line), "ply %zu   %zu legal", game.plyCount(),
                  game.legalMoves().size());
    rightAlign(line, small, t.boneDim);
    if (result != GameResult::InProgress) {
      rightAlign(("by " + std::string(toString(game.endReason()))).c_str(), small,
                 t.boneFaint);
    } else if (game.inCheck()) {
      rightAlign("IN CHECK", small, t.blood);
    }
  }
  ImGui::End();
  ImGui::PopStyleVar();

  // ------------------------------------------------------- recent moves, bottom right
  if (game.plyCount() > 0) {
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin,
                                   vp->WorkPos.y + vp->WorkSize.y - margin),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, px(1)));
    ImGui::Begin("##recent", nullptr, overlayFlags);
    {
      // The last handful of moves, not a ledger. The full history belongs on a screen
      // someone opened on purpose, not permanently over the board.
      const auto& moves = game.moveHistory();
      const std::size_t show = std::min<std::size_t>(moves.size(), 6);
      ImGui::PushFont(small);
      for (std::size_t i = moves.size() - show; i < moves.size(); ++i) {
        const bool last = i + 1 == moves.size();
        char row[96];
        std::snprintf(row, sizeof(row), "%2zu%s %s", i / 2 + 1, (i % 2 == 0) ? "." : "..",
                      moveText(v, moves[i]).c_str());
        const float width = ImGui::CalcTextSize(row).x;
        const float avail = ImGui::GetContentRegionAvail().x;
        if (avail > width) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - width);
        ImGui::PushStyleColor(ImGuiCol_Text, col(last ? t.ember : t.boneFaint));
        ImGui::TextUnformatted(row);
        ImGui::PopStyleColor();
      }
      ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }

  // ------------------------------------------------------------ controls, bottom left
  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + margin, vp->WorkPos.y + vp->WorkSize.y - margin),
      ImGuiCond_Always, ImVec2(0.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##controls", nullptr, overlayFlags);
  {
    if (button("UNDO", t, px(78))) {
      app::Action a;
      a.kind = app::ActionKind::Undo;
      (void)session.apply(a);
    }
    ImGui::SameLine();
    if (button("RESET", t, px(84))) {
      app::Action a;
      a.kind = app::ActionKind::Reset;
      (void)session.apply(a);
    }
    if (v.dims.dims() > 2) {
      ImGui::SameLine();
      // Cold, because it acts on the geometry rather than on the game.
      if (button("AXES", t, px(78), false, true)) {
        const DimSpec& d = v.dims;
        bool temporal = false;
        for (std::uint8_t ax = 0; ax < d.dims(); ++ax) {
          if (d.kind(ax) != AxisKind::Spatial) temporal = true;
        }
        app::Action a;
        a.kind = app::ActionKind::SetScreenAxes;
        if (temporal) {
          // Time travel is unreadable in a depth view, so a temporal board is only ever
          // shown with its spatial axes on screen and the boards laid out in full. The
          // control just swaps whether time runs across the screen or down it.
          std::string text;
          for (std::uint8_t ax = 0; ax < d.dims(); ++ax) {
            if (d.kind(ax) != AxisKind::Spatial) continue;
            if (!text.empty()) text += ",";
            text += d.name(ax);
          }
          a.text = text;
          axisRotation_ ^= 1;
          a.gridVertical = axisRotation_ != 0;
        } else {
          const int dims = static_cast<int>(d.dims());
          // The default view, then every pair of axes twice: once with the extra axes
          // laid across the screen and once down it. A 3-D board's depth axis is only
          // visible as a column of boards, so both arrangements have to be in the cycle.
          const int steps = 1 + dims * 2;
          axisRotation_ = (axisRotation_ + 1) % steps;
          if (axisRotation_ == 0) {
            a.text = "default";
          } else {
            const int s = axisRotation_ - 1;
            const int r = (s / 2) % dims;
            a.gridVertical = (s % 2) == 1;
            a.text = d.name(static_cast<std::size_t>(r)) + "," +
                     d.name(static_cast<std::size_t>((r + 1) % dims));
          }
        }
        (void)session.apply(a);
      }
    }
    ImGui::SameLine();
    if (button("MENU", t, px(78))) shell.pause();

    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    if (game.plyCount() == 0) {
      // Only while it is still useful; a permanent instruction line is clutter.
      ImGui::TextUnformatted(
          v.geom.isBox()
              ? "click a piece, then a lit cell   -   right-drag to orbit"
              : "cyan edges are joined to each other   -   right-drag to orbit");
    } else if (!session.message().empty()) {
      ImGui::TextUnformatted(session.message().c_str());
    }
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::End();
  ImGui::PopStyleVar();

  // --------------------------------------------------------------------- frame rate
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin,
                                 vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(1.0f, 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("##fps", nullptr, overlayFlags);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint, 0.55f));
  ImGui::Text("%3.0f fps", static_cast<double>(fps));
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::End();
  ImGui::PopStyleVar();

  // ------------------------------------------------------------------ promotion
  const auto& pending = session.pendingPromotion();
  if (pending.active) {
    ImGui::OpenPopup("promotion");
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            "promotion", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
      ImGui::PushFont(display);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
      ImGui::Text("PROMOTE ON %s", cellName(v.dims, pending.to).c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, px(4)));

      // Built from the variant's own promotion list, so a variant that promotes to a
      // unicorn offers a unicorn. Never a hardcoded four.
      for (std::size_t i = 0; i < pending.choices.size(); ++i) {
        if (i != 0) ImGui::SameLine();
        const PieceTypeId piece = pending.choices[i];
        if (button(upper(v.pieces[piece].name).c_str(), t, px(104), i == 0)) {
          (void)session.choosePromotion(piece);
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::Dummy(ImVec2(0, px(2)));
      if (button("CANCEL", t, px(104))) {
        session.cancelPromotion();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  // A flat board's pieces are drawn here rather than by the renderer: a circle token and
  // the piece's own icon, in screen space, which is also what lets the token follow the
  // move animation exactly. The background draw list puts them over the board but under
  // the panels, so a pause menu covers them rather than the other way round.
  if (session.flatView()) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    // These are drawn after the board, and therefore after the defocus pass. Fading them
    // out as the board goes soft is what stops a paused board showing sharp pieces
    // standing on blurred squares.
    const float focus = 1.0f - session.pullBack();
    const float w = vp->Size.x;
    const float h = vp->Size.y;
    const view::OrbitCamera& cam = session.camera();
    // One world unit is one cell along the drawn axes, so projecting it gives the
    // on-screen cell pitch and the token can be sized to the board instead of a guess.
    const view::OrbitCamera::ScreenPoint o0 =
        cam.project(view::Vec3{0, 0, 0}, w / h, w, h);
    const view::OrbitCamera::ScreenPoint o1 =
        cam.project(view::Vec3{1, 0, 0}, w / h, w, h);
    const float cellPx = std::max(px(6.0f), std::hypot(o1.x - o0.x, o1.y - o0.y));
    const float radius = cellPx * 0.34f;

    const auto drawToken = [&](const Piece& piece, const view::Vec3& world) {
      if (piece.empty()) return;
      const view::OrbitCamera::ScreenPoint sp = cam.project(world, w / h, w, h);
      if (!sp.visible) return;
      const bool white = piece.colorOf() == Color::White;
      const view::Rgba token = white ? t.whitePiece : t.blackPiece;
      const ImVec2 centre(sp.x, sp.y);
      if (focus <= 0.01f) return;
      dl->AddCircleFilled(centre, radius, u32(token, focus), 24);
      dl->AddCircle(centre, radius, u32(t.boardRim, focus), 24, px(1.5f));
      drawPieceGlyph(dl, centre, radius * 0.82f,
                     u32(white ? t.blackPiece : t.whitePiece, focus), u32(token, focus),
                     archetypeFor(v.pieces[piece.type]), iconStyle_);
    };

    const view::MoveAnimation& anim = session.animation();
    const CellId movingTo = anim.active() ? anim.travellingTo() : kInvalidCell;
    for (const view::Placement& pl : session.placements()) {
      if (pl.cell == movingTo) continue;  // the moving piece is drawn at its own point
      drawToken(session.snapshot().at(pl.cell), view::Vec3{pl.x, pl.y, pl.z});
    }
    if (movingTo != kInvalidCell) {
      const view::MoveAnimation::Sample at = anim.sample();
      if (at.moving) {
        drawToken(session.snapshot().at(movingTo), view::Vec3{at.x, at.y, at.z});
      }
    }
  }

  return request;
}

void Ui::endFrame() {
  ImGui::Render();
}

void Ui::record(VkCommandBuffer cmd) {
  ImDrawData* data = ImGui::GetDrawData();
  if (data != nullptr) ImGui_ImplVulkan_RenderDrawData(data, cmd);
}

Ui::~Ui() {
  if (initialised_) {
    vkDeviceWaitIdle(ctx_->device());
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
  }
  if (pool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(ctx_->device(), pool_, nullptr);
}

}  // namespace cb::render
