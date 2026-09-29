// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/ui.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "io/notation.hpp"
#include "render/offscreen_target.hpp"

namespace cb::render {
namespace {

ImVec4 col(const view::Rgba& c, float alpha = 1.0f) {
  return ImVec4{c.r, c.g, c.b, c.a * alpha};
}
ImU32 u32(const view::Rgba& c, float alpha = 1.0f) {
  return ImGui::ColorConvertFloat4ToU32(col(c, alpha));
}

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
void plateHeading(const char* text, const view::Theme& theme, ImFont* font) {
  ImGui::PushFont(font);
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneDim));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  ImGui::PopFont();
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const float y = (min.y + max.y) * 0.5f;
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  if (right > max.x + 8.0f) {
    ImGui::GetWindowDrawList()->AddLine(ImVec2(max.x + 8.0f, y), ImVec2(right, y),
                                        u32(theme.rule), 1.0f);
  }
  ImGui::Dummy(ImVec2(0, 2));
}

void keyValue(const char* key, const std::string& value, const view::Theme& theme,
              bool cold = false) {
  ImGui::PushStyleColor(ImGuiCol_Text, col(theme.boneFaint));
  ImGui::TextUnformatted(key);
  ImGui::PopStyleColor();
  ImGui::SameLine();
  const float width = ImGui::GetContentRegionAvail().x;
  const float textWidth = ImGui::CalcTextSize(value.c_str()).x;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, width - textWidth));
  ImGui::PushStyleColor(ImGuiCol_Text, col(cold ? theme.rift : theme.bone));
  ImGui::TextUnformatted(value.c_str());
  ImGui::PopStyleColor();
}

/// A chunky, bevelled button with a hard drop shadow - it should look pressable rather
/// than like a tinted rectangle.
bool plateButton(const char* label, const view::Theme& theme, const view::Rgba& accent,
                 bool primary, float width = 0.0f) {
  const ImVec2 size{width > 0 ? width : 0.0f, 0.0f};
  ImGui::PushStyleColor(ImGuiCol_Button, primary ? col(accent) : col(theme.panel));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        primary ? col(accent, 0.85f) : col(theme.panelHi));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, col(accent));
  ImGui::PushStyleColor(ImGuiCol_Text, primary ? col(theme.ink) : col(theme.boneDim));
  ImGui::PushStyleColor(ImGuiCol_Border, primary ? col(accent) : col(theme.rule));
  const bool pressed = ImGui::Button(label, size);
  ImGui::PopStyleColor(5);
  return pressed;
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
  const auto display = findFont("Cinzel-Bold.ttf");
  const auto body = findFont("JetBrainsMono-Regular.ttf");

  if (!body.empty()) {
    fontBody_ = io.Fonts->AddFontFromFileTTF(body.string().c_str(), 15.0f);
    fontSmall_ = io.Fonts->AddFontFromFileTTF(body.string().c_str(), 12.0f);
  } else {
    fontBody_ = io.Fonts->AddFontDefault();
    fontSmall_ = fontBody_;
  }
  if (!display.empty()) {
    ImFontConfig cfg;
    // Cinzel is used only for uppercase names, and tracking it out is what makes it
    // read as struck into brass rather than typed.
    cfg.GlyphExtraSpacing.x = 1.4f;
    fontDisplay_ = io.Fonts->AddFontFromFileTTF(display.string().c_str(), 17.0f, &cfg);
  } else {
    // A missing typeface must not stop the game starting; it just looks plainer.
    fontDisplay_ = fontBody_;
  }
  io.FontDefault = static_cast<ImFont*>(fontBody_);
  return {};
}

void Ui::applyStyle() {
  ImGuiStyle& s = ImGui::GetStyle();
  // Machined edges. Rounding is the single thing that makes ImGui look like a debug
  // overlay, so the panels get none and the controls get just enough to look milled.
  s.WindowRounding = 0.0f;
  s.ChildRounding = 0.0f;
  s.FrameRounding = 2.0f;
  s.PopupRounding = 0.0f;
  s.ScrollbarRounding = 0.0f;
  s.GrabRounding = 2.0f;
  s.WindowBorderSize = 1.0f;
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
  c[ImGuiCol_ModalWindowDimBg] = col(t.ink, 0.72f);
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

UiRequest Ui::build(app::Session& session, const std::vector<std::string>& library,
                    const std::string& currentVariant, float fps) {
  UiRequest request;
  const view::Theme& t = theme_;
  const VariantSpec& v = session.variant();
  const Game& game = session.game();
  auto* display = static_cast<ImFont*>(fontDisplay_);
  auto* small = static_cast<ImFont*>(fontSmall_);

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float railWidth = std::clamp(vp->Size.x * 0.17f, 190.0f, 280.0f);
  // The board belongs in the gap the rails leave, not behind them.
  request.boardRect[0] = railWidth;
  request.boardRect[1] = 0.0f;
  request.boardRect[2] = std::max(64.0f, vp->Size.x - 2.0f * railWidth);
  request.boardRect[3] = std::max(64.0f, vp->Size.y - 30.0f);
  const ImGuiWindowFlags railFlags =
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBringToFrontOnFocus;

  // ---------------------------------------------------------------- left rail
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(ImVec2(railWidth, vp->WorkSize.y));
  ImGui::Begin("##left", nullptr, railFlags);
  {
    plateHeading("VARIANT", t, small);

    // The nameplate: engraved, bordered in the accent, the one place the variant's own
    // name is stated at full size.
    const ImVec2 plateMin = ImGui::GetCursorScreenPos();
    const float plateWidth = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col(t.panelHi));
    ImGui::BeginChild("##plate", ImVec2(plateWidth, 54), ImGuiChildFlags_Border);
    ImGui::PushFont(display);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
    std::string upper = currentVariant;
    for (char& ch : upper) ch = static_cast<char>(std::toupper(ch));
    ImGui::TextUnformatted(upper.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
    ImGui::TextWrapped("%s", v.geom.isBox() ? "flat board" : "glued board");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::GetWindowDrawList()->AddRect(plateMin,
                                        ImVec2(plateMin.x + plateWidth, plateMin.y + 54),
                                        u32(t.emberDeep), 0.0f, 0, 1.0f);

    ImGui::Dummy(ImVec2(0, 4));
    plateHeading("BOARD", t, small);
    ImGui::PushFont(small);
    keyValue("axes", std::to_string(v.dims.dims()), t);
    keyValue("cells", std::to_string(v.dims.cellCount()), t);
    keyValue("geometry",
             v.geom.isBox() ? "box" : (v.geom.isOrientable() ? "glued" : "twisted"), t,
             !v.geom.isBox());
    keyValue("directions", std::to_string(v.dirTable.size()), t);
    keyValue("pieces", std::to_string(v.pieces.size() - 1), t);
    if (v.ruleSet) keyValue("rules", "yes", t);
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 4));
    plateHeading("LIBRARY", t, small);
    ImGui::PushFont(small);
    for (const std::string& name : library) {
      const bool active = name == currentVariant;
      ImGui::PushStyleColor(ImGuiCol_Text, col(active ? t.bone : t.boneDim));
      if (ImGui::Selectable(name.c_str(), active)) request.loadVariant = name;
      ImGui::PopStyleColor();
      if (active) {
        // A lit edge marks the loaded variant, rather than a filled row that would
        // fight the nameplate above it.
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x - 4, min.y),
                                            ImVec2(min.x - 4, max.y), u32(t.ember), 2.0f);
      }
    }
    ImGui::PopFont();
  }
  ImGui::End();

  // --------------------------------------------------------------- right rail
  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + vp->WorkSize.x - railWidth, vp->WorkPos.y));
  ImGui::SetNextWindowSize(ImVec2(railWidth, vp->WorkSize.y));
  ImGui::Begin("##right", nullptr, railFlags);
  {
    plateHeading("STATUS", t, small);
    const GameResult result = game.result();
    ImGui::PushFont(display);
    if (result == GameResult::InProgress) {
      const bool white = game.position().sideToMove() == Color::White;
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.ember));
      ImGui::TextUnformatted(white ? "WHITE TO MOVE" : "BLACK TO MOVE");
      ImGui::PopStyleColor();
    } else {
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
      std::string text(toString(result));
      for (char& ch : text) ch = static_cast<char>(std::toupper(ch));
      ImGui::TextUnformatted(text.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::PopFont();

    ImGui::PushFont(small);
    ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneDim));
    ImGui::Text("ply %zu  -  %zu legal move%s", game.plyCount(), game.legalMoves().size(),
                game.legalMoves().size() == 1 ? "" : "s");
    ImGui::PopStyleColor();
    if (result != GameResult::InProgress) {
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::Text("by %s", std::string(toString(game.endReason())).c_str());
      ImGui::PopStyleColor();
    } else if (game.inCheck()) {
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.blood));
      ImGui::TextUnformatted("in check");
      ImGui::PopStyleColor();
    }
    if (!session.message().empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::TextWrapped("%s", session.message().c_str());
      ImGui::PopStyleColor();
    }
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 6));
    plateHeading("LEDGER", t, small);
    const float controlsHeight = 108.0f;
    ImGui::BeginChild(
        "##ledger",
        ImVec2(0, std::max(60.0f, ImGui::GetContentRegionAvail().y - controlsHeight)),
        ImGuiChildFlags_Border);
    ImGui::PushFont(small);
    const auto& moves = game.moveHistory();
    for (std::size_t i = 0; i < moves.size(); i += 2) {
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::Text("%2zu", i / 2 + 1);
      ImGui::PopStyleColor();
      ImGui::SameLine(30.0f);
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.bone));
      ImGui::TextUnformatted(moveText(v, moves[i]).c_str());
      ImGui::PopStyleColor();
      if (i + 1 < moves.size()) {
        ImGui::SameLine(96.0f);
        const bool last = i + 2 >= moves.size();
        ImGui::PushStyleColor(ImGuiCol_Text, col(last ? t.ember : t.boneDim));
        ImGui::TextUnformatted(moveText(v, moves[i + 1]).c_str());
        ImGui::PopStyleColor();
      }
    }
    if (moves.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
      ImGui::TextUnformatted("no moves yet");
      ImGui::PopStyleColor();
    }
    // Follow the game as it is played.
    if (ImGui::GetScrollMaxY() > 0) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont();
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 2));
    const float full = ImGui::GetContentRegionAvail().x;
    const float half = (full - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (plateButton("UNDO", t, t.ember, false, half)) {
      app::Action a;
      a.kind = app::ActionKind::Undo;
      (void)session.apply(a);
    }
    ImGui::SameLine();
    if (plateButton("RESET", t, t.ember, false, half)) {
      app::Action a;
      a.kind = app::ActionKind::Reset;
      (void)session.apply(a);
    }
    // Re-aiming the view is a cold control: it acts on geometry, not on the game.
    if (v.dims.dims() > 2) {
      if (plateButton("RE-AIM AXES", t, t.rift, false, full)) {
        app::Action a;
        a.kind = app::ActionKind::SetScreenAxes;
        const DimSpec& d = v.dims;
        static int rotation = 0;
        rotation = (rotation + 1) % d.dims();
        a.text = d.name(static_cast<std::size_t>(rotation)) + "," +
                 d.name(static_cast<std::size_t>((rotation + 1) % d.dims()));
        (void)session.apply(a);
      }
    }
    if (plateButton("QUIT", t, t.blood, false, full)) request.quit = true;
  }
  ImGui::End();

  // -------------------------------------------------------------- bottom hint
  const float hintHeight = 30.0f;
  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + railWidth, vp->WorkPos.y + vp->WorkSize.y - hintHeight));
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - 2 * railWidth, hintHeight));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, col(t.ink, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
  ImGui::Begin("##hint", nullptr,
               railFlags | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
  ImGui::PushFont(small);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint));
  ImGui::Text(
      "click a piece, then a lit cell   -   right-drag to orbit   -   wheel to zoom%s",
      v.geom.isBox() ? "" : "   -   cyan edges are glued to each other");
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 60.0f);
  ImGui::PushStyleColor(ImGuiCol_Text, col(t.boneFaint, 0.7f));
  ImGui::Text("%3.0f fps", static_cast<double>(fps));
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

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
      ImGui::Dummy(ImVec2(0, 4));

      // Built from the variant's own promotion list, so a variant that promotes to a
      // unicorn offers a unicorn. Never a hardcoded four.
      for (std::size_t i = 0; i < pending.choices.size(); ++i) {
        if (i != 0) ImGui::SameLine();
        const PieceTypeId piece = pending.choices[i];
        std::string label = v.pieces[piece].name;
        for (char& ch : label) ch = static_cast<char>(std::toupper(ch));
        if (plateButton(label.c_str(), t, t.ember, i == 0, 96.0f)) {
          (void)session.choosePromotion(piece);
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::Dummy(ImVec2(0, 2));
      if (plateButton("CANCEL", t, t.blood, false, 0.0f)) {
        session.cancelPromotion();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
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
