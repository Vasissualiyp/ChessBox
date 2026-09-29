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
/// The shape follows the piece's archetype, so a variant's pieces are distinguishable
/// without anyone drawing art for them: a pawn is a ball, a rook a crenellated block, a
/// knight a wedge, and so on. It is deliberately not a lettered tile - the flat board is
/// meant to read as a board, not as a spreadsheet.
void drawPieceGlyph(ImDrawList* dl, ImVec2 c, float r, ImU32 col, Archetype shape) {
  switch (shape) {
    case Archetype::Dome:  // pawn
      dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.15f), r * 0.5f, col, 16);
      dl->AddRectFilled(ImVec2(c.x - r * 0.7f, c.y + r * 0.35f),
                        ImVec2(c.x + r * 0.7f, c.y + r * 0.62f), col, r * 0.15f);
      break;
    case Archetype::Tower:  // rook
      dl->AddRectFilled(ImVec2(c.x - r * 0.62f, c.y - r * 0.45f),
                        ImVec2(c.x + r * 0.62f, c.y + r * 0.7f), col, r * 0.1f);
      for (int i = -1; i <= 1; ++i) {
        dl->AddRectFilled(
            ImVec2(c.x + static_cast<float>(i) * r * 0.42f - r * 0.16f, c.y - r * 0.85f),
            ImVec2(c.x + static_cast<float>(i) * r * 0.42f + r * 0.16f, c.y - r * 0.45f),
            col);
      }
      break;
    case Archetype::Wedge:  // knight
      dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.8f),
                            ImVec2(c.x - r * 0.75f, c.y + r * 0.65f),
                            ImVec2(c.x + r * 0.75f, c.y + r * 0.65f), col);
      break;
    case Archetype::Spire:  // bishop
      dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.9f),
                            ImVec2(c.x - r * 0.5f, c.y + r * 0.2f),
                            ImVec2(c.x + r * 0.5f, c.y + r * 0.2f), col);
      dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.4f), r * 0.48f, col, 16);
      break;
    case Archetype::Crown:  // queen
      dl->AddRectFilled(ImVec2(c.x - r * 0.7f, c.y + r * 0.25f),
                        ImVec2(c.x + r * 0.7f, c.y + r * 0.65f), col);
      for (int i = -1; i <= 1; ++i) {
        const float dx = static_cast<float>(i) * r * 0.6f;
        dl->AddTriangleFilled(ImVec2(c.x + dx, c.y - r * 0.85f),
                              ImVec2(c.x + dx - r * 0.24f, c.y + r * 0.25f),
                              ImVec2(c.x + dx + r * 0.24f, c.y + r * 0.25f), col);
      }
      break;
    case Archetype::Monolith:  // king, or anything royal
      dl->AddRectFilled(ImVec2(c.x - r * 0.17f, c.y - r * 0.9f),
                        ImVec2(c.x + r * 0.17f, c.y + r * 0.9f), col);
      dl->AddRectFilled(ImVec2(c.x - r * 0.7f, c.y - r * 0.35f),
                        ImVec2(c.x + r * 0.7f, c.y + r * 0.0f), col);
      break;
    case Archetype::Horn:  // unicorn and friends
      dl->AddQuadFilled(ImVec2(c.x, c.y - r * 0.9f), ImVec2(c.x + r * 0.5f, c.y),
                        ImVec2(c.x, c.y + r * 0.9f), ImVec2(c.x - r * 0.5f, c.y), col);
      break;
    default:
      dl->AddCircleFilled(c, r * 0.6f, col, 16);
      break;
  }
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
  const auto display = findFont("Cinzel-Bold.ttf");
  const auto body = findFont("JetBrainsMono-Regular.ttf");

  if (!body.empty()) {
    fontBody_ = io.Fonts->AddFontFromFileTTF(body.string().c_str(), 15.0f * scale_);
    fontSmall_ = io.Fonts->AddFontFromFileTTF(body.string().c_str(), 12.0f * scale_);
  } else {
    fontBody_ = io.Fonts->AddFontDefault();
    fontSmall_ = fontBody_;
  }
  if (!display.empty()) {
    ImFontConfig cfg;
    // Cinzel is used only for uppercase names, and tracking it out is what makes it
    // read as struck into brass rather than typed.
    cfg.GlyphExtraSpacing.x = 1.4f * scale_;
    fontDisplay_ =
        io.Fonts->AddFontFromFileTTF(display.string().c_str(), 17.0f * scale_, &cfg);
  } else {
    // A missing typeface must not stop the game starting; it just looks plainer.
    fontDisplay_ = fontBody_;
  }
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

void Ui::newFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

UiRequest Ui::build(app::Shell& shell, float fps) {
  switch (shell.screen()) {
    case app::Screen::MainMenu:
      return buildMainMenu(shell);
    case app::Screen::NewGame:
      return buildNewGame(shell);
    case app::Screen::Creator:
      return buildCreator(shell);
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
      UiRequest out = buildGameHud(shell, fps);
      const UiRequest pause = buildPause(shell);
      out.quit = out.quit || pause.quit;
      return out;
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
      dl->AddCircleFilled(centre, radius, u32(token), 24);
      dl->AddCircle(centre, radius, u32(t.ink), 24, px(1.5f));
      drawPieceGlyph(dl, centre, radius * 0.82f, u32(white ? t.ink : t.bone),
                     archetypeFor(v.pieces[piece.type]));
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
