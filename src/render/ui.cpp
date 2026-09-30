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
#include "render/overture_scene.hpp"
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
    // Concave, and properly so: a rook's crenellations, a queen's coronet and any
    // valley an author draws are not convex hulls, and a fill that treats them as one
    // closes the notches over.
    fillPolygon(dl, dl->_Path.Data, dl->_Path.Size, fill);
    dl->PathClear();
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
  // Keyboard navigation is on: the whole menu tree is reachable without a mouse, and the
  // focus ring is drawn in the accent so it matches the rest (see applyStyle).
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

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
  // The keyboard-navigation ring, in the accent, so a nav-driven menu reads like the
  // rest.
  c[ImGuiCol_NavCursor] = col(t.ember);
  c[ImGuiCol_NavWindowingHighlight] = col(t.ember, 0.7f);

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
  lastDt_ = dt;
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

void Ui::updateObjectDrag(app::Shell& shell, const ImVec2& min, const ImVec2& max) {
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 m = io.MousePos;
  const bool inside = m.x >= min.x && m.x < max.x && m.y >= min.y && m.y < max.y;
  // The grab has to *start* on the object. Testing "inside" every frame would let a
  // drag that began on a menu row keep turning the object once the pointer crossed the
  // hairline, which feels like the interface grabbing at the cursor.
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && inside && !io.WantCaptureMouse) {
    objectDrag_ = true;
  }
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) objectDrag_ = false;
  if (!objectDrag_) return;

  const app::Settings& set = shell.settings();
  const float k = 0.007f * set.orbitSensitivity;
  objectYaw_ += io.MouseDelta.x * k;
  // Dragging down lowers the camera, the same way round as orbiting the board, and the
  // same setting inverts both.
  objectElev_ -= io.MouseDelta.y * k * (set.invertOrbitY ? -1.0f : 1.0f);
  // Kept off the poles: an object seen exactly edge-on is a line, and one turned past
  // the vertical is being looked at from underneath - which is the thing that most
  // reliably reads as a bug rather than as a camera.
  objectElev_ = std::clamp(objectElev_, -1.15f, 1.15f);
}

void Ui::drawOvertureObject(app::Shell& shell, const ImVec2& min, const ImVec2& max,
                            float zoom, float alpha) {
  const app::OverturePlayer& p = shell.overtures();
  OvertureScene scene = overtureScene(p.current(), p.progress(), p.intro(), theme_);
  // The player's turn is added to the overture's own camera, not substituted for it, so
  // a shape taken hold of mid-morph carries on morphing.
  scene.cam.yaw += objectYaw_;
  scene.cam.elev = std::clamp(scene.cam.elev + objectElev_, 0.10f, 1.53f);
  drawOverture(ImGui::GetBackgroundDrawList(), scene, min, max, theme_, iconStyle_, zoom,
               alpha);
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
           iconStyle_, clock_, subjectFor(shell.screen()), inZoom, inE, objectYaw_,
           objectElev_);
  if (leaving_ != Deco::None && outE < 1.0f) {
    const float outZoom = deeper_ ? 1.0f + 0.8f * outE : 1.0f - 0.5f * outE;
    const VariantSpec* leavingSubject =
        leavingScreen_ >= 0 ? subjectFor(static_cast<app::Screen>(leavingScreen_))
                            : nullptr;
    drawDeco(dl, leaving_, decoMin, decoMax, theme_, iconStyle_, clock_, leavingSubject,
             outZoom, 1.0f - outE, objectYaw_, objectElev_);
  }

  // The piece designer's object *is* a board: the selected piece at its centre and the
  // cells its atoms reach, in the half the decoration would otherwise occupy.
  if (shell.screen() == app::Screen::Editor && editorPage_ == 1) {
    drawEditorPreview(shell, decoMin, decoMax);
  }
  // Whatever object this screen shows, the player can take hold of it and turn it.
  updateObjectDrag(shell, decoMin, decoMax);
  // And the library's object is the selected variant's overture: the 8x8 board becoming
  // whatever that variant plays on, and back again. It replaces the turning lattice,
  // which said only how many boards there were.
  if (shell.screen() == app::Screen::NewGame) {
    drawOvertureObject(shell, decoMin, decoMax, inZoom, inE);
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

  // A piece is declared once and played on whatever board a variant declares, so the
  // question "what does this do on a four-dimensional board" is one the designer has to
  // answer. The extent shrinks as the dimension grows for the obvious reason: nine to
  // the fourth is six and a half thousand cells and nothing can be read in it.
  const int dims = std::clamp(previewDims_, 2, 4);
  const int extent = dims == 2 ? 9 : (dims == 3 ? 7 : 5);
  std::vector<AxisDecl> decls;
  static const char* kAxisNames[4]{"file", "rank", "level", "aeon"};
  for (int i = 0; i < dims; ++i) {
    AxisDecl d;
    d.extent = extent;
    d.name = kAxisNames[i];
    decls.push_back(d);
  }
  auto spec = DimSpec::create(decls);
  if (!spec.has_value()) return;
  const DimSpec& ds = *spec;
  const view::ViewConfig cfg = view::ViewConfig::forBoard(ds);
  const std::vector<view::Placement> cells = view::layout(ds, cfg);
  if (cells.empty()) return;

  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const view::Theme& t = theme_;
  dl->AddRectFilled(min, max, u32(t.ink));

  // Where the piece stands: the middle of the board on every axis.
  Coord centre(static_cast<std::uint8_t>(dims));
  for (std::size_t i = 0; i < static_cast<std::size_t>(dims); ++i) {
    centre.c[i] = static_cast<std::int16_t>(extent / 2);
  }
  const CellId origin = ds.toCell(centre);

  std::vector<std::uint8_t> mark(cells.size() == 0 ? 0 : (ds.cellCount()), 0);
  const int orientIndex = editor->orientationAxis() == 0 ? 0 : 1;
  for (const MoveAtom& a : *atoms) {
    if (a.mode == MoveMode::Hop) continue;  // needs a hurdle; not modelled here
    const std::vector<Direction> dirs =
        a.oriented
            ? expandAtomOriented(a.mags, static_cast<std::uint8_t>(dims),
                                 static_cast<std::uint8_t>(orientIndex), Color::White)
            : expandAtom(a.mags, static_cast<std::uint8_t>(dims));
    const int first = std::max(1, static_cast<int>(a.minK));
    const int last = a.maxK == kUnlimited
                         ? extent - 1
                         : std::min(static_cast<int>(a.maxK), extent - 1);
    for (const Direction& d : dirs) {
      for (int n = first; n <= last; ++n) {
        Coord p = centre;
        for (std::uint8_t k = 0; k < d.nsup; ++k) {
          const std::uint8_t ax = d.sup[k];
          p.c[ax] = static_cast<std::int16_t>(p.c[ax] + n * d.v[ax]);
        }
        if (!ds.inRange(p)) break;
        const CellId c = ds.toCell(p);
        // Only the two-dimensional board carries the scratch pieces, so above it every
        // ray simply runs to the edge - which is the thing worth seeing there anyway.
        std::uint8_t occ = 0;
        if (dims == 2) {
          const int gx = p.c[0];
          const int gy = extent - 1 - p.c[1];
          if (gx >= 0 && gx < kPreviewN && gy >= 0 && gy < kPreviewN) {
            occ =
                previewCells_[static_cast<std::size_t>(gy)][static_cast<std::size_t>(gx)];
          }
        }
        if (occ != 0) {
          if (occ == 1 && a.capture != CapturePolicy::Cannot) mark[c] = 2;
          if (a.mode != MoveMode::Leap) break;
          continue;
        }
        if (a.capture != CapturePolicy::Must && mark[c] == 0) mark[c] = 1;
      }
    }
  }

  // The camera. A flat board is looked at from almost straight down, as a diagram; a
  // solid one is stepped back so its depth reads. The drag adds to both, so the same
  // gesture that turns the menu's object turns this board.
  const float yaw = (dims == 2 ? 0.0f : 0.62f) + objectYaw_;
  const float elev = std::clamp((dims == 2 ? 1.50f : 0.92f) + objectElev_, 0.12f, 1.55f);
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);
  const float ce = std::cos(elev);
  const float se = std::sin(elev);
  const view::Bounds bounds = view::boundsOf(cells);
  const auto raw = [&](float x, float y, float z) {
    const float dx = x - bounds.centerX();
    const float dy = y - bounds.centerY();
    const float dz = z - bounds.centerZ();
    const float x1 = dx * cy + dy * sy;
    const float y1 = -dx * sy + dy * cy;
    const float up = dz * ce + y1 * se;
    const float depth = -dz * se + y1 * ce;
    return std::array<float, 3>{x1, -up, depth};
  };
  float ext = 0.001f;
  for (const view::Placement& pl : cells) {
    const auto q = raw(pl.x, pl.y, pl.z);
    ext = std::max(ext, std::max(std::abs(q[0]), std::abs(q[1])));
  }
  const float span = std::min(max.x - min.x, max.y - min.y);
  const float scale = span * 0.40f / (ext + 0.8f);
  const ImVec2 mid((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const auto at = [&](float x, float y, float z) {
    const auto q = raw(x, y, z);
    return ImVec2(mid.x + q[0] * scale, mid.y + q[1] * scale);
  };

  struct Tile {
    ImVec2 p[4];
    float depth{0};
    CellId cell{0};
    bool light{false};
  };
  std::vector<Tile> tiles;
  tiles.reserve(cells.size());
  for (const view::Placement& pl : cells) {
    const Coord p = ds.toCoord(pl.cell);
    int parity = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(dims); ++i) {
      parity += p.c[i];
    }
    Tile tile;
    tile.p[0] = at(pl.x - 0.46f, pl.y - 0.46f, pl.z);
    tile.p[1] = at(pl.x + 0.46f, pl.y - 0.46f, pl.z);
    tile.p[2] = at(pl.x + 0.46f, pl.y + 0.46f, pl.z);
    tile.p[3] = at(pl.x - 0.46f, pl.y + 0.46f, pl.z);
    tile.depth = raw(pl.x, pl.y, pl.z)[2];
    tile.cell = pl.cell;
    tile.light = (parity & 1) == 0;
    tiles.push_back(tile);
  }
  // Far to near: a board of boards overlaps itself from every useful angle, and without
  // the sort the far ones paint over the near ones.
  std::sort(tiles.begin(), tiles.end(),
            [](const Tile& a, const Tile& b) { return a.depth > b.depth; });

  const bool solid = dims == 2;
  for (const Tile& tile : tiles) {
    const view::Rgba base = tile.light ? t.boardLight : t.boardDark;
    // Above two dimensions the board is mostly cells the piece cannot reach - a knight
    // on a 5^4 lattice lights 48 of 625 - so the ones it can have to win. The board
    // steps back and the marks stay at full strength.
    const bool lit = mark[tile.cell] != 0;
    const float ground = solid ? 1.0f : (lit ? 0.9f : 0.34f);
    dl->AddQuadFilled(tile.p[0], tile.p[1], tile.p[2], tile.p[3], u32(base, ground));
    if (mark[tile.cell] == 2) {
      dl->AddQuadFilled(tile.p[0], tile.p[1], tile.p[2], tile.p[3], u32(t.blood, 0.7f));
    } else if (mark[tile.cell] == 1) {
      dl->AddQuadFilled(tile.p[0], tile.p[1], tile.p[2], tile.p[3], u32(t.moss, 0.65f));
    }
    if (tile.cell == origin) {
      dl->AddQuad(tile.p[0], tile.p[1], tile.p[2], tile.p[3], u32(t.ember), 2.0f);
    }
  }

  // The scratch pieces, and the piece itself, on the flat board only.
  PieceTypeDef shapeDef;
  shapeDef.atoms = *atoms;
  const Archetype shape = archetypeFor(shapeDef);
  const float cell =
      std::hypot(at(1, 0, 0).x - at(0, 0, 0).x, at(1, 0, 0).y - at(0, 0, 0).y);
  if (dims == 2) {
    for (int gy = 0; gy < kPreviewN; ++gy) {
      for (int gx = 0; gx < kPreviewN; ++gx) {
        const std::uint8_t occ =
            previewCells_[static_cast<std::size_t>(gy)][static_cast<std::size_t>(gx)];
        if (occ == 0) continue;
        Coord p(2);
        p.c[0] = static_cast<std::int16_t>(gx);
        p.c[1] = static_cast<std::int16_t>(extent - 1 - gy);
        if (!ds.inRange(p)) continue;
        for (const view::Placement& pl : cells) {
          if (pl.cell != ds.toCell(p)) continue;
          const ImVec2 c = at(pl.x, pl.y, pl.z);
          dl->AddCircleFilled(c, cell * 0.36f, u32(t.pieceToken), 18);
          drawPieceGlyph(dl, c, cell * 0.30f, u32(occ == 1 ? t.blackPiece : t.whitePiece),
                         u32(t.pieceToken), Archetype::Dome, iconStyle_);
          break;
        }
      }
    }
  }
  for (const view::Placement& pl : cells) {
    if (pl.cell != origin) continue;
    const ImVec2 c = at(pl.x, pl.y, pl.z);
    dl->AddCircleFilled(c, cell * 0.36f, u32(t.pieceToken), 18);
    drawPieceGlyph(dl, c, cell * 0.30f, u32(t.whitePiece), u32(t.pieceToken), shape,
                   iconStyle_);
  }

  // The click target for the scratch board, on the flat board only: above two dimensions
  // a drag has to reach the board to turn it, and a window over it would eat the drag.
  if (dims == 2) {
    const view::Placement* first = nullptr;
    for (const view::Placement& pl : cells) {
      if (first == nullptr || pl.x < first->x || pl.y > first->y) first = &pl;
    }
    if (first != nullptr) {
      previewOrigin_ = ImVec2(at(-0.5f, static_cast<float>(extent) - 0.5f, 0.0f));
      previewCell_ = cell;
      previewValid_ = true;
    }
  }
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

void Ui::drawCheckEdges(app::Shell& shell) {
  if (!shell.showsCheckWarning()) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 min = vp->WorkPos;
  const ImVec2 max(vp->WorkPos.x + vp->WorkSize.x, vp->WorkPos.y + vp->WorkSize.y);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  // A soft red vignette at the frame's edge: a few nested outlines, strongest at the
  // very edge and fading inward, so the whole screen reads as "in check" without a panel.
  constexpr int kRings = 7;
  const float band = px(9.0f);
  for (int i = 0; i < kRings; ++i) {
    const float inset = static_cast<float>(i) * band;
    const float alpha = 0.30f * (1.0f - static_cast<float>(i) / kRings);
    dl->AddRect(ImVec2(min.x + inset, min.y + inset),
                ImVec2(max.x - inset, max.y - inset), u32(theme_.blood, alpha), 0.0f, 0,
                band);
  }
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
    // The library always opens on standard. It is the board every overture departs from,
    // so it is the one that makes the next choice legible - and reopening on whatever
    // was picked last time means arriving mid-thought at a shape with no context.
    // Not when a capture has already said what it wants to see: `--shot --screen newgame`
    // pins the overture before the first frame is built, and this hook runs on that very
    // frame - so without the guard every capture would come back showing standard.
    if (shell.screen() == app::Screen::NewGame && !shell.overtures().pinned()) {
      const auto& lib = shell.library();
      if (std::find(lib.begin(), lib.end(), "standard") != lib.end()) {
        pickedVariant_ = "standard";
        shell.overtures().jumpTo(app::Overture::Standard);
      }
    }
  }

  // The departing menu first, so the arriving one is drawn over it.
  drawGhost(shell);

  UiRequest request;
  switch (shell.screen()) {
    case app::Screen::MainMenu:
      request = buildMainMenu(shell);
      break;
    case app::Screen::NewGame:
      request = buildNewGame(shell);
      break;
    case app::Screen::Editor:
      request = buildEditor(shell);
      break;
    case app::Screen::QuitConfirm:
      request = buildQuitConfirm(shell);
      break;
    case app::Screen::PauseQuitConfirm:
      request = buildPauseQuitConfirm(shell);
      break;
    case app::Screen::Settings:
      request = buildSettings(shell);
      break;
    case app::Screen::GameInfo:
      request = buildGameInfo(shell);
      break;
    case app::Screen::PieceMoves:
      request = buildPieceMoves(shell);
      break;
    case app::Screen::Paused:
      request = buildPause(shell);
      break;
    case app::Screen::Game:
      request = buildGameHud(shell, fps);
      break;
  }
  drawCheckEdges(shell);
  return request;
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

  // File and rank labels on a flat board's near edges, when the setting asks. Projected
  // through the same camera the board is drawn with, so a label sits on its cell.
  if (shell.settings().showCoordinates && v.dims.dims() == 2) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const view::OrbitCamera cam = session.camera();
    const float w = vp->Size.x;
    const float h = vp->Size.y;
    for (const view::Placement& pl : session.placements()) {
      const Coord co = v.dims.toCoord(pl.cell);
      const view::OrbitCamera::ScreenPoint sp =
          cam.project(view::Vec3{pl.x, pl.y, pl.z}, w / h, w, h);
      if (!sp.visible) continue;
      if (co.c[1] == 0) {
        const std::string file(1, static_cast<char>('a' + co.c[0]));
        dl->AddText(ImVec2(sp.x - px(4), sp.y + px(9)), u32(t.boneFaint), file.c_str());
      }
      if (co.c[0] == 0) {
        const std::string rank = std::to_string(co.c[1] + 1);
        dl->AddText(ImVec2(sp.x - px(12), sp.y - px(5)), u32(t.boneFaint), rank.c_str());
      }
    }
  }

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
    // Name the screen axes, so a 3-D or 4-D board says which axis runs which way: a 4-D
    // board otherwise raises "which of these four is up?" as its first question.
    if (shell.settings().showCoordinates) {
      const auto& axes = session.viewConfig().screenAxes;
      std::string legend;
      for (std::size_t i = 0; i < axes.size(); ++i) {
        if (i != 0) legend += "   ";
        legend += v.dims.name(axes[i]);
        legend += i == 0 ? " ->" : " ^";
      }
      ImGui::PushFont(small);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::TextUnformatted(legend.c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
    }
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
    if (session.hotSeat()) {
      // Whose turn, and the coordinates entered so far, so a player can see the square
      // they are spelling out before it completes.
      const Color mover = game.position().sideToMove();
      const auto partial = session.hotSeatKeys().partial(mover);
      std::string line = mover == Color::White ? "p1 (white): " : "p2 (black): ";
      if (partial.empty()) {
        line += mover == Color::White ? "qwertasdfgzxcvb" : "yuiophjkl;nm,./";
        line += "   -   one key per axis, then the next square";
      } else {
        for (std::int16_t c : partial) line += std::to_string(c) + " ";
      }
      ImGui::TextUnformatted(line.c_str());
    } else if (game.plyCount() == 0) {
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

  // A move waiting for confirmation: a yes or a no, and nothing else. The board is frozen
  // behind it until it is answered.
  const auto& waiting = session.pendingMove();
  if (waiting.active) {
    ImGui::OpenPopup("confirmmove");
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
                                   vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(
            "confirmmove", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
      ImGui::PushFont(display);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
      ImGui::Text("PLAY %s - %s?", cellName(v.dims, waiting.from).c_str(),
                  cellName(v.dims, waiting.to).c_str());
      ImGui::PopStyleColor();
      ImGui::PopFont();
      ImGui::Dummy(ImVec2(0, px(4)));
      if (button("PLAY", t, px(104), true, false, true, display)) {
        (void)session.confirmMove();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (button("CANCEL", t, px(104), false, false, true, display)) {
        session.cancelMove();
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
      const ImVec2 centre(sp.x, sp.y);
      if (focus <= 0.01f) return;
      // One token colour for both sides, with the figure in its own. The disc is
      // furniture - it lifts the piece off the square and gives the outline something
      // to sit on - and the thing being read is the figure.
      dl->AddCircleFilled(centre, radius, u32(t.pieceToken, focus), 24);
      dl->AddCircle(centre, radius, u32(t.boardRim, focus), 24, px(1.5f));
      drawPieceGlyph(
          dl, centre, radius * 0.82f, u32(white ? t.whitePiece : t.blackPiece, focus),
          u32(t.pieceToken, focus), archetypeFor(v.pieces[piece.type]), iconStyle_);
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

  // Two players, one keyboard: put each square's keys on the board, for the player to
  // move, so the mapping is learnt in place rather than read off a line of legend. The
  // key list is indexed by coordinate on every axis, so a cell's label is one character
  // per board axis. Drawn in screen space over the board, flat or solid.
  if (session.hotSeat()) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const Color mover = game.position().sideToMove();
    const float w = vp->Size.x;
    const float h = vp->Size.y;
    const view::OrbitCamera& cam = session.camera();
    const float size = small != nullptr ? small->FontSize : ImGui::GetFontSize();
    for (const view::Placement& pl : session.placements()) {
      if (!session.boardVisible(pl.cell)) continue;
      const std::string label = app::HotSeat::keysFor(mover, v.dims.toCoord(pl.cell));
      if (label.empty()) continue;
      const view::OrbitCamera::ScreenPoint sp =
          cam.project(view::Vec3{pl.x, pl.y, pl.z}, w / h, w, h);
      if (!sp.visible) continue;
      const ImVec2 ts = small != nullptr
                            ? small->CalcTextSizeA(size, FLT_MAX, 0.0f, label.c_str())
                            : ImGui::CalcTextSize(label.c_str());
      const ImVec2 at(sp.x - ts.x * 0.5f, sp.y - ts.y * 0.5f);
      // A small chip behind the letters keeps them legible on a light or a dark square.
      dl->AddRectFilled(ImVec2(at.x - px(3.0f), at.y - px(1.0f)),
                        ImVec2(at.x + ts.x + px(3.0f), at.y + ts.y + px(1.0f)),
                        u32(t.soot, 0.72f), px(3.0f));
      if (small != nullptr) {
        dl->AddText(small, size, at, u32(t.bone), label.c_str());
      } else {
        dl->AddText(at, u32(t.bone), label.c_str());
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
