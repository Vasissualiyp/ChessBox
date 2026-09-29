// SPDX-License-Identifier: GPL-3.0-or-later
//
// The playable window.
//
// Deliberately thin: every decision it makes is delegated to app::Session, which is
// covered by scripted tests, and every pixel comes from the renderer and the interface,
// which are covered by headless image tests. What is left here - translating events and
// deciding when to redraw - is the only part that genuinely cannot be tested without a
// display, so there is as little of it as possible.
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app/session.hpp"
#include "io/variant_toml.hpp"
#include "render/board_renderer.hpp"
#include "render/image_io.hpp"
#include "render/window.hpp"
#ifdef CB_HAVE_IMGUI
#include "render/ui.hpp"
#endif

using namespace cb;

namespace {

/// Where the variants live, whether running from the build tree or an install.
std::filesystem::path variantDir() {
  for (const char* base : {"variants", "../variants", "../../variants",
                           "share/chessbox/variants", "/usr/share/chessbox/variants"}) {
    if (std::filesystem::is_directory(base)) return base;
  }
  return "variants";
}

std::vector<std::string> variantLibrary() {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(variantDir(), ec)) {
    if (e.is_regular_file() && e.path().extension() == ".toml") {
      names.push_back(e.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::unique_ptr<app::Session> openVariant(const std::string& name, std::string& error) {
  auto variant = loadVariantFile(variantDir() / (name + ".toml"));
  if (!variant.has_value()) {
    error = variant.error().format();
    return nullptr;
  }
  auto session = app::Session::create(std::move(*variant));
  if (!session.has_value()) {
    error = session.error().format();
    return nullptr;
  }
  return std::move(*session);
}

}  // namespace

/// Render a single frame - board and interface together - to a file and exit.
///
/// It needs no display: SDL's dummy video driver supplies a window for the interface's
/// input mapping, and the frame goes to the same offscreen target the headless tests
/// use. That makes the *whole* screen, panels included, capturable on a machine with no
/// compositor - which is how the interface gets reviewed at all.
int captureFrame(const std::string& variantName, const std::string& path,
                 const std::string& script) {
  std::string error;
  auto session = openVariant(variantName, error);
  if (session == nullptr) {
    std::fprintf(stderr, "cannot load '%s': %s\n", variantName.c_str(), error.c_str());
    return 1;
  }
  if (!script.empty()) {
    if (auto ok = session->applyScript(script); !ok.has_value()) {
      std::fprintf(stderr, "script: %s\n", ok.error().format().c_str());
      return 1;
    }
  }

  if (SDL_getenv("DISPLAY") == nullptr && SDL_getenv("WAYLAND_DISPLAY") == nullptr) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("ChessBox", 1440, 900, SDL_WINDOW_HIDDEN);
  if (window == nullptr) {
    std::fprintf(stderr, "cannot create a hidden window: %s\n", SDL_GetError());
    return 1;
  }

  auto ctx = render::VulkanContext::create();
  if (!ctx.has_value()) {
    std::fprintf(stderr, "%s\n", ctx.error().format().c_str());
    return 1;
  }
  auto renderer = render::BoardRenderer::create(*ctx);
  auto target = render::OffscreenTarget::create(*ctx, 1440, 900);
  if (!renderer.has_value() || !target.has_value()) {
    std::fprintf(stderr, "cannot set up the renderer\n");
    return 1;
  }

#ifdef CB_HAVE_IMGUI
  auto ui = render::Ui::create(*ctx, window, renderer->theme());
  if (!ui.has_value()) {
    std::fprintf(stderr, "%s\n", ui.error().format().c_str());
    return 1;
  }
  // Two frames: ImGui sizes some things from the previous frame, so the first one can
  // show a panel mid-layout.
  for (int frame = 0; frame < 2; ++frame) {
    (*ui)->newFrame();
    const render::UiRequest request =
        (*ui)->build(*session, variantLibrary(), variantName, 60.0f);
    (*ui)->endFrame();
    const auto instances =
        renderer->buildInstances(session->snapshot(), session->viewConfig());
    const render::BoardRect rect{request.boardRect[0], request.boardRect[1],
                                 request.boardRect[2], request.boardRect[3]};
    if (auto ok = renderer->render(
            *target, instances, session->camera(),
            [&](VkCommandBuffer cmd) { (*ui)->record(cmd); }, rect);
        !ok.has_value()) {
      std::fprintf(stderr, "%s\n", ok.error().format().c_str());
      return 1;
    }
  }
#else
  const auto instances =
      renderer->buildInstances(session->snapshot(), session->viewConfig());
  (void)renderer->render(*target, instances, session->camera());
#endif

  auto pixels = target->readPixels();
  if (!pixels.has_value()) {
    std::fprintf(stderr, "%s\n", pixels.error().format().c_str());
    return 1;
  }
  render::Image img;
  img.width = target->width();
  img.height = target->height();
  img.rgba = std::move(*pixels);
  if (auto ok = render::writePpm(img, path); !ok.has_value()) {
    std::fprintf(stderr, "%s\n", ok.error().format().c_str());
    return 1;
  }
  const std::size_t validation = ctx->validationErrorCount();
  for (const std::string& message : ctx->takeValidationMessages()) {
    std::fprintf(stderr, "%s\n", message.c_str());
  }
  std::printf("wrote %s\n", path.c_str());
  return validation == 0 ? 0 : 2;
}

int main(int argc, char** argv) {
  std::string variantName = "standard";
  std::string shotPath;
  std::string script;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      std::printf(
          "chessbox_gui [variant] [--shot FILE] [--script TEXT]\n\n"
          "  left click    select a piece, then a lit cell to move it\n"
          "  right drag    orbit     wheel  zoom\n"
          "  u  undo       r  reset  q/Esc  quit\n\n"
          "Variants are listed in the left rail; anything in variants/ shows up there.\n"
          "--shot renders one frame to a PPM and exits, with no display required.\n"
          "--script runs actions first, e.g. 'click e2\\nclick e4'.\n");
      return 0;
    }
    if (arg == "--shot" && i + 1 < argc) {
      shotPath = argv[++i];
    } else if (arg == "--script" && i + 1 < argc) {
      script = argv[++i];
    } else if (!arg.starts_with("-")) {
      variantName = arg;
    }
  }
  if (!shotPath.empty()) return captureFrame(variantName, shotPath, script);

  std::string error;
  auto session = openVariant(variantName, error);
  if (session == nullptr) {
    std::fprintf(stderr, "cannot load '%s': %s\n", variantName.c_str(), error.c_str());
    return 1;
  }

  auto window = render::Window::create("ChessBox", 1440, 900);
  if (!window.has_value()) {
    std::fprintf(stderr, "cannot open a window: %s\n", window.error().format().c_str());
    return 1;
  }
  auto renderer = render::BoardRenderer::create(window->context());
  if (!renderer.has_value()) {
    std::fprintf(stderr, "cannot create the renderer: %s\n",
                 renderer.error().format().c_str());
    return 1;
  }
  auto target = render::OffscreenTarget::create(window->context(), window->width(),
                                                window->height());
  if (!target.has_value()) {
    std::fprintf(stderr, "cannot create a render target: %s\n",
                 target.error().format().c_str());
    return 1;
  }

#ifdef CB_HAVE_IMGUI
  auto ui = render::Ui::create(window->context(), window->handle(), renderer->theme());
  if (!ui.has_value()) {
    std::fprintf(stderr, "cannot create the interface: %s\n",
                 ui.error().format().c_str());
    return 1;
  }
#endif

  const std::vector<std::string> library = variantLibrary();
  bool running = true;
  bool orbiting = false;
  render::BoardRect boardRect{0, 0, static_cast<float>(window->width()),
                              static_cast<float>(window->height())};
  auto lastFrame = std::chrono::steady_clock::now();
  float fps = 0.0f;

  while (running) {
    SDL_Event e;
    // Wait for input rather than spinning: a board game has nothing to animate, and
    // redrawing an unchanged position would only heat the room. A short timeout keeps
    // the frame counter and any hover feedback alive.
    if (!SDL_WaitEventTimeout(&e, 100)) {
      e.type = SDL_EVENT_POLL_SENTINEL;
    }
    do {
      if (e.type == SDL_EVENT_POLL_SENTINEL) continue;
#ifdef CB_HAVE_IMGUI
      const bool consumed = (*ui)->processEvent(e);
#else
      const bool consumed = false;
#endif
      switch (e.type) {
        case SDL_EVENT_QUIT:
          running = false;
          break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
          const auto w = static_cast<std::uint32_t>(e.window.data1);
          const auto h = static_cast<std::uint32_t>(e.window.data2);
          if (w > 0 && h > 0) {
            (void)window->recreate(w, h);
            auto fresh = render::OffscreenTarget::create(window->context(), w, h);
            if (fresh.has_value()) target = std::move(*fresh);
          }
          break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          if (consumed) break;
          if (e.button.button == SDL_BUTTON_LEFT) {
            // Picking uses the same rectangle the board was drawn into, or a click
            // would land on a different cell than the one under the cursor.
            session->clickPixel(e.button.x - boardRect.x, e.button.y - boardRect.y,
                                boardRect.width, boardRect.height);
          } else if (e.button.button == SDL_BUTTON_RIGHT) {
            orbiting = true;
          }
          break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
          if (e.button.button == SDL_BUTTON_RIGHT) orbiting = false;
          break;
        case SDL_EVENT_MOUSE_MOTION:
          if (orbiting) {
            app::Action a;
            a.kind = app::ActionKind::Orbit;
            a.dx = e.motion.xrel * 0.01f;
            a.dy = e.motion.yrel * -0.01f;
            (void)session->apply(a);
          }
          break;
        case SDL_EVENT_MOUSE_WHEEL: {
          if (consumed) break;
          app::Action a;
          a.kind = app::ActionKind::Zoom;
          a.dx = e.wheel.y > 0 ? 0.9f : 1.1f;
          (void)session->apply(a);
          break;
        }
        case SDL_EVENT_KEY_DOWN: {
          if (consumed) break;
          app::Action a;
          switch (e.key.key) {
            case SDLK_Q:
            case SDLK_ESCAPE:
              running = false;
              break;
            case SDLK_U:
              a.kind = app::ActionKind::Undo;
              (void)session->apply(a);
              break;
            case SDLK_R:
              a.kind = app::ActionKind::Reset;
              (void)session->apply(a);
              break;
            default:
              break;
          }
          break;
        }
        default:
          break;
      }
    } while (SDL_PollEvent(&e));

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastFrame).count();
    lastFrame = now;
    if (dt > 0.0f) fps = fps * 0.9f + (1.0f / dt) * 0.1f;

    // The board shows what just happened and who is in trouble; both come from the
    // engine, so the renderer has no opinion of its own to be wrong about.
    const auto& history = session->game().moveHistory();
    if (history.empty()) {
      renderer->setLastMove(kInvalidCell, kInvalidCell);
    } else {
      renderer->setLastMove(history.back().from, history.back().to);
    }
    renderer->setCheckCell(session->game().inCheck()
                               ? session->game().position().findRoyal(
                                     session->game().position().sideToMove())
                               : kInvalidCell);

#ifdef CB_HAVE_IMGUI
    (*ui)->newFrame();
    const render::UiRequest request = (*ui)->build(*session, library, variantName, fps);
    (*ui)->endFrame();
    if (request.boardRect[2] > 0) {
      boardRect = render::BoardRect{request.boardRect[0], request.boardRect[1],
                                    request.boardRect[2], request.boardRect[3]};
    }
    if (request.quit) running = false;
    if (!request.loadVariant.empty() && request.loadVariant != variantName) {
      std::string loadError;
      if (auto fresh = openVariant(request.loadVariant, loadError); fresh != nullptr) {
        session = std::move(fresh);
        variantName = request.loadVariant;
      } else {
        std::fprintf(stderr, "cannot load '%s': %s\n", request.loadVariant.c_str(),
                     loadError.c_str());
      }
    }
#endif

    const auto instances =
        renderer->buildInstances(session->snapshot(), session->viewConfig());
    const auto overlay = [&](VkCommandBuffer cmd) {
#ifdef CB_HAVE_IMGUI
      (*ui)->record(cmd);
#else
      (void)cmd;
#endif
    };
    if (auto ok =
            renderer->render(*target, instances, session->camera(), overlay, boardRect);
        !ok.has_value()) {
      std::fprintf(stderr, "render failed: %s\n", ok.error().format().c_str());
      break;
    }
    if (auto ok = window->present(*target); !ok.has_value()) {
      std::fprintf(stderr, "present failed: %s\n", ok.error().format().c_str());
      break;
    }
  }
  return 0;
}
