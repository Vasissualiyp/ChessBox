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
#include "pieces/atom.hpp"
#include "render/deco.hpp"
#include "render/offscreen_target.hpp"
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

/// The shell's own screens - the main menu and the panels it opens into. They move as
/// panes and a departing one is worth drawing again; the board screens are covered by
/// the board's own pull-back instead.
bool isMenuScreen(app::Screen s) noexcept {
  switch (s) {
    case app::Screen::MainMenu:
    case app::Screen::NewGame:
    case app::Screen::Editor:
    case app::Screen::Settings:
    case app::Screen::QuitConfirm:
    case app::Screen::PauseQuitConfirm:
    case app::Screen::Paused:
    case app::Screen::GameInfo:
    case app::Screen::PieceMoves:
      return true;
    default:
      return false;
  }
}

/// How far in the pause branch a screen is: the game is in front (0), pause is a step
/// back (1), and the reference and quit panels are another step (2). Screens outside the
/// branch return -1.
int pauseRank(app::Screen s) noexcept {
  switch (s) {
    case app::Screen::Game:
      return 0;
    case app::Screen::Paused:
      return 1;
    case app::Screen::GameInfo:
    case app::Screen::PieceMoves:
    case app::Screen::PauseQuitConfirm:
      return 2;
    default:
      return -1;
  }
}

/// Which way the camera travels when moving from one screen to another.
///
/// On the shell's own menus, deeper is "a greater screen depth": opening a panel moves
/// forward into it. The pause branch is the reverse - the game is in front, pressing
/// pause steps back, and opening a reference panel steps back again - so there it is the
/// *lower* rank that is forward. The main-menu quit prompt is the other exception: it
/// sits outside the menu, so opening it backs out too.
bool goesDeeper(app::Screen from, app::Screen to) noexcept {
  if (to == app::Screen::QuitConfirm) return false;
  if (from == app::Screen::QuitConfirm) return true;
  const int fromRank = pauseRank(from);
  const int toRank = pauseRank(to);
  if (fromRank >= 0 && toRank >= 0) return toRank < fromRank;
  return app::screenDepth(to) >= app::screenDepth(from);
}

/// The transition in two halves, so the screens do not cross-fade. The departing screen
/// is finished and gone by `kOutEnd`; the arriving one does not begin until `kInStart`.
/// The gap is the point: with both moving at once the eye reads one screen turning into
/// another, where a short pause reads as passing through the first into the second.
constexpr float kOutEnd = 0.40f;
constexpr float kInStart = 0.55f;

float smoothstep(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
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
  const ImVec2 max =
      ImVec2(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);

  // The object stays on one side across screens. Alternating it by depth made the
  // decoration jump from one half of the frame to the other as the screen changed, which
  // reads as a teleport; keeping it put lets the one clock zoom it in place, so a deeper
  // screen looks like the camera moving towards the same object rather than a new one.
  const bool decoLeft = true;
  // Most screens split the frame in half. The designer needs a wider pane for its rows
  // of controls, so it takes the larger share and leaves the preview a narrower strip.
  const float menuShare = shell.screen() == app::Screen::Editor ? 0.42f : 0.5f;
  const float split = min.x + (max.x - min.x) * menuShare;
  const ImVec2 decoMin(decoLeft ? min.x : split, min.y);
  const ImVec2 decoMax(decoLeft ? split : max.x, max.y);
  menuMin = ImVec2(decoLeft ? split : min.x, min.y);
  menuMax = ImVec2(decoLeft ? max.x : split, max.y);
  // Remembered so a screen that owns its own object - the piece reference and its move
  // diagrams - can put it in the same place the deco would have gone.
  decoMin_ = decoMin;
  decoMax_ = decoMax;

  // A dragging ghost only needs the rectangle: the ground, the field and the arriving
  // object are the arriving screen's to draw, and drawing them twice would double them.
  if (ghosting_) return;

  // The ground. A light shell has to paint its own, or the board's clear colour shows
  // through where nothing else is drawn.
  dl->AddRectFilled(min, max, u32(theme_.ink));
  field_.draw(dl, min, max, theme_, iconStyle_, true, true);

  // A screen's object belongs to that screen: the library's lattice is the *selected*
  // variant's, while a board screen shows the running game's. The departing ghost must
  // ask for its own subject rather than inherit the arriving one - otherwise the lattice
  // left behind by "new game" would briefly wear the running game's boards.
  const auto subjectFor = [&](app::Screen s) -> const VariantSpec* {
    if (s == app::Screen::NewGame) return shell.preview(pickedVariant_);
    return shell.hasGame() ? &shell.session()->variant() : nullptr;
  };
  // The departing screen's object leaves first; the arriving screen's only grows in once
  // it is gone. The two halves of the clock are separate for exactly that reason.
  const float inE = inEase();
  const float outE = outEase();
  const float inZoom = deeper_ ? 0.55f + 0.45f * inE : 1.7f - 0.7f * inE;
  drawDeco(dl, decoForScreen(static_cast<int>(shell.screen())), decoMin, decoMax, theme_,
           iconStyle_, clock_, subjectFor(shell.screen()), inZoom, inE);
  if (leaving_ != Deco::None && outE < 1.0f) {
    const float outZoom = deeper_ ? 1.0f + 0.8f * outE : 1.0f - 0.5f * outE;
    const VariantSpec* leavingSubject =
        leavingScreen_ >= 0 ? subjectFor(static_cast<app::Screen>(leavingScreen_))
                            : nullptr;
    drawDeco(dl, leaving_, decoMin, decoMax, theme_, iconStyle_, clock_, leavingSubject,
             outZoom, 1.0f - outE);
  }

  // The piece designer's object *is* a board: the selected piece at its centre and the
  // cells its atoms reach, in the half the decoration would otherwise occupy.
  if (shell.screen() == app::Screen::Editor && editorPage_ == 1) {
    drawEditorPreview(shell, decoMin, decoMax);
  }

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

void Ui::drawEditorPreview(app::Shell& shell, const ImVec2& min, const ImVec2& max) {
  previewValid_ = false;
  const app::Editor* editor = shell.editor();
  if (editor == nullptr) return;
  const std::vector<std::string> names = editor->pieceNames();
  if (names.empty()) return;
  const int index = std::clamp(editorPiece_, 0, static_cast<int>(names.size()) - 1);
  auto atoms = editor->pieceAtoms(names[static_cast<std::size_t>(index)]);
  if (!atoms.has_value()) return;

  constexpr int kN = kPreviewN;
  const ImVec2 span(max.x - min.x, max.y - min.y);
  const float side = std::min(span.x, span.y) * 0.72f;
  const float cell = side / static_cast<float>(kN);
  const ImVec2 origin(min.x + (span.x - side) * 0.5f, min.y + (span.y - side) * 0.5f);
  previewOrigin_ = origin;
  previewCell_ = cell;
  previewValid_ = true;

  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const view::Theme& t = theme_;
  // The board replaces the decorative object here: the object *is* the board.
  dl->AddRectFilled(min, max, u32(t.ink));
  const auto baseColour = [&](int x, int y) {
    return ((x + y) & 1) != 0 ? t.boardDark : t.boardLight;
  };
  PieceTypeDef shapeDef;
  shapeDef.atoms = *atoms;
  const Archetype shape = archetypeFor(shapeDef);

  bool quiet[kN][kN] = {};
  bool capture[kN][kN] = {};
  const int cx = kN / 2;
  const int cy = kN / 2;
  // A 2-D preview only has two axes; the variant's forward axis is whichever of them it
  // declared (standard chess means the rank), and anything else reads as the second.
  const int orientIndex = editor->orientationAxis() == 0 ? 0 : 1;
  const auto up = static_cast<std::size_t>(orientIndex);
  const auto right = static_cast<std::size_t>(orientIndex == 0 ? 1 : 0);
  for (const MoveAtom& a : *atoms) {
    if (a.mode == MoveMode::Hop) continue;  // needs a hurdle; not modelled here
    const std::vector<Direction> dirs =
        a.oriented ? expandAtomOriented(a.mags, 2, static_cast<std::uint8_t>(orientIndex),
                                        Color::White)
                   : expandAtom(a.mags, 2);
    // Step range from the move's own min/max: a rider runs to the edge, an exact-n move
    // like a pawn's double step lands only where it says.
    const int first = std::max(1, static_cast<int>(a.minK));
    const int last =
        a.maxK == kUnlimited ? kN - 1 : std::min(static_cast<int>(a.maxK), kN - 1);
    for (const Direction& d : dirs) {
      for (int n = first; n <= last; ++n) {
        // The forward axis is drawn up the screen and the other to the right, so a
        // forward-only piece reads as moving up, the way a player expects to see it.
        const int x = cx + n * d.v[right];
        const int y = cy - n * d.v[up];
        if (x < 0 || x >= kN || y < 0 || y >= kN) break;
        const std::uint8_t occ =
            previewCells_[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
        if (occ != 0) {
          // A black piece here can be taken if the move allows it. A slider stops at it;
          // a leap carries on over it.
          if (occ == 1 && a.capture != CapturePolicy::Cannot) capture[y][x] = true;
          if (a.mode != MoveMode::Leap) break;
          continue;
        }
        if (a.capture != CapturePolicy::Must) quiet[y][x] = true;
      }
    }
  }

  for (int y = 0; y < kN; ++y) {
    for (int x = 0; x < kN; ++x) {
      const ImVec2 a(origin.x + static_cast<float>(x) * cell,
                     origin.y + static_cast<float>(y) * cell);
      const ImVec2 b(a.x + cell, a.y + cell);
      dl->AddRectFilled(a, b, u32(baseColour(x, y)));
      if (capture[y][x]) {
        dl->AddRectFilled(a, b, u32(t.blood, 0.5f));
      } else if (quiet[y][x]) {
        dl->AddRectFilled(a, b, u32(t.moss, 0.45f));
      }
    }
  }
  dl->AddRect(origin, ImVec2(origin.x + side, origin.y + side), u32(t.rule, 0.8f));

  // Pieces the player placed are always pawns - the piece under test is the one at the
  // centre - then the piece being designed, in white, at the centre.
  for (int y = 0; y < kN; ++y) {
    for (int x = 0; x < kN; ++x) {
      const std::uint8_t occ =
          previewCells_[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
      if (occ == 0) continue;
      const ImVec2 c(origin.x + (static_cast<float>(x) + 0.5f) * cell,
                     origin.y + (static_cast<float>(y) + 0.5f) * cell);
      const view::Rgba col = occ == 1 ? t.blackPiece : t.whitePiece;
      drawPieceGlyph(dl, c, cell * 0.42f, u32(col), u32(baseColour(x, y)),
                     Archetype::Dome, iconStyle_);
    }
  }
  const ImVec2 centre(origin.x + (static_cast<float>(cx) + 0.5f) * cell,
                      origin.y + (static_cast<float>(cy) + 0.5f) * cell);
  drawPieceGlyph(dl, centre, cell * 0.42f, u32(t.whitePiece), u32(baseColour(cx, cy)),
                 shape, iconStyle_);
}

void Ui::pauseFrame(UiRequest& request, ImVec2& menuMin, ImVec2& menuMax) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 vmin = vp->WorkPos;
  const ImVec2 vmax(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
  const float split = vmin.x + (vmax.x - vmin.x) * 0.52f;
  menuMin = ImVec2(split, vmin.y);
  menuMax = ImVec2(vmax.x, vmax.y);
  // The board keeps the left of the frame; a screen that draws its own object - the move
  // diagrams - puts it there.
  decoMin_ = vmin;
  decoMax_ = ImVec2(split, vmax.y);
  // A departing ghost must not lay the scrim a second time.
  if (ghosting_) return;
  ImGui::GetBackgroundDrawList()->AddRectFilled(vmin, vmax, u32(theme_.ink, 0.42f));
  request.boardRect[0] = vmin.x;
  request.boardRect[1] = vmin.y;
  request.boardRect[2] = split - vmin.x;
  request.boardRect[3] = vmax.y - vmin.y;
}

float Ui::outEase() const noexcept {
  return smoothstep(enter_ / kOutEnd);
}

float Ui::inEase() const noexcept {
  return smoothstep((enter_ - kInStart) / (1.0f - kInStart));
}

Ui::PaneMove Ui::paneMove() const noexcept {
  // Pause and the reference panels sit over the board and do not travel as panes; they
  // keep the old grow-in, which rides only the arriving half of the clock.
  const int which = ghosting_ ? leavingScreen_ : currentScreen_;
  if (!isMenuScreen(static_cast<app::Screen>(which))) {
    const float inE = inEase();
    return {0.66f + 0.34f * inE, inE};
  }
  if (ghosting_) {
    // The screen being left swells towards the viewer on the way in, and shrinks away on
    // the way back out. It has faded to nothing by the time the next one starts.
    const float outE = outEase();
    return {deeper_ ? 1.0f + 0.35f * outE : 1.0f - 0.34f * outE, 1.0f - outE};
  }
  // The screen arriving grows from small on the way in; on the way back it returns from
  // the size the outgoing one left it at, so the two moves are reverses.
  const float inE = inEase();
  return {deeper_ ? 0.66f + 0.34f * inE : 1.35f - 0.35f * inE, inE};
}

void Ui::drawGhost(app::Shell& shell) {
  if (leavingScreen_ < 0 || outEase() >= 1.0f) return;
  const auto leaving = static_cast<app::Screen>(leavingScreen_);
  if (!isMenuScreen(leaving) || !isMenuScreen(static_cast<app::Screen>(currentScreen_))) {
    return;
  }
  // Disabled while it is drawn: a departing screen must be seen but not touched. Its
  // request is discarded too, so re-running its build can neither start a game nor
  // change a setting.
  ghosting_ = true;
  ImGui::BeginDisabled();
  switch (leaving) {
    case app::Screen::MainMenu:
      (void)buildMainMenu(shell);
      break;
    case app::Screen::NewGame:
      (void)buildNewGame(shell);
      break;
    case app::Screen::Editor:
      (void)buildEditor(shell);
      break;
    case app::Screen::Settings:
      (void)buildSettings(shell);
      break;
    case app::Screen::QuitConfirm:
      (void)buildQuitConfirm(shell);
      break;
    case app::Screen::PauseQuitConfirm:
      (void)buildPauseQuitConfirm(shell);
      break;
    case app::Screen::Paused:
      (void)buildPause(shell);
      break;
    case app::Screen::GameInfo:
      (void)buildGameInfo(shell);
      break;
    case app::Screen::PieceMoves:
      (void)buildPieceMoves(shell);
      break;
    default:
      break;
  }
  ImGui::EndDisabled();
  ghosting_ = false;
}

UiRequest Ui::build(app::Shell& shell, float fps) {
  // A screen change starts the camera moving and shoves the field towards the viewer -
  // or away from it, on the way back out.
  const int screen = static_cast<int>(shell.screen());
  currentScreen_ = screen;
  if (screen != lastScreen_) {
    const bool deeper = lastScreen_ < 0 ||
                        goesDeeper(static_cast<app::Screen>(lastScreen_), shell.screen());
    // Going in, the field rushes towards and past the viewer; coming back it recedes.
    // The impulse is negative for "towards the viewer" - see DepthField::push.
    field_.push(deeper ? -3.1f : 2.6f);
    // Remember what is leaving, and from which screen, so its pane and its decoration can
    // be drawn once more on the way out. The ghost's input is disabled and its request
    // discarded, so re-running its build cannot navigate.
    leaving_ = lastScreen_ >= 0 ? decoForScreen(lastScreen_) : Deco::None;
    leavingScreen_ = lastScreen_;
    deeper_ = deeper;
    enter_ = 0.0f;
    lastScreen_ = screen;
  }

  // The departing menu first, so the arriving one is drawn over it.
  drawGhost(shell);

  switch (shell.screen()) {
    case app::Screen::MainMenu:
      return buildMainMenu(shell);
    case app::Screen::NewGame:
      return buildNewGame(shell);
    case app::Screen::Editor:
      return buildEditor(shell);
    case app::Screen::QuitConfirm:
      return buildQuitConfirm(shell);
    case app::Screen::PauseQuitConfirm:
      return buildPauseQuitConfirm(shell);
    case app::Screen::Settings:
      return buildSettings(shell);
    case app::Screen::GameInfo:
      return buildGameInfo(shell);
    case app::Screen::PieceMoves:
      return buildPieceMoves(shell);
    case app::Screen::Paused:
      return buildPause(shell);
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
    if (button("UNDO", t, px(78), false, false, true, display)) {
      app::Action a;
      a.kind = app::ActionKind::Undo;
      (void)session.apply(a);
    }
    ImGui::SameLine();
    if (button("RESET", t, px(84), false, false, true, display)) {
      app::Action a;
      a.kind = app::ActionKind::Reset;
      (void)session.apply(a);
    }
    if (v.dims.dims() > 2) {
      ImGui::SameLine();
      // Cold, because it acts on the geometry rather than on the game.
      if (button("AXES", t, px(78), false, true, true, display)) {
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
    // View mode travels with the player: a flat board is easier to read, a solid one
    // easier to understand. Cold, because it is a view of the geometry, not a move.
    if (button(session.flatView() ? "2D" : "3D", t, px(64), false, true, true, display)) {
      const bool flat = !session.flatView();
      session.setFlatView(flat);
      shell.settings().flatView = flat;
      request.settingsChanged = true;
    }
    ImGui::SameLine();
    if (button("MENU", t, px(78), false, false, true, display)) shell.pause();

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
        if (button(upper(v.pieces[piece].name).c_str(), t, px(104), i == 0, false, true,
                   display)) {
          (void)session.choosePromotion(piece);
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::Dummy(ImVec2(0, px(2)));
      if (button("CANCEL", t, px(104), false, false, true, display)) {
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
