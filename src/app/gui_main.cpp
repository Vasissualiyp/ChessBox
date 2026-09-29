// SPDX-License-Identifier: GPL-3.0-or-later
// The playable window.
//
// Deliberately thin: every decision it makes is delegated to app::Session, which is
// covered by scripted tests, and every pixel comes from render::BoardRenderer, which is
// covered by headless image tests. What is left here - event translation and a redraw
// loop - is the part that genuinely cannot be tested without a display, so there is as
// little of it as possible.
#include <SDL3/SDL.h>

#include <cstdio>
#include <string>

#include "app/session.hpp"
#include "io/variant_toml.hpp"
#include "render/board_renderer.hpp"
#include "render/window.hpp"

using namespace cb;

namespace {

std::filesystem::path variantPath(const std::string& name) {
  const std::filesystem::path direct(name);
  if (std::filesystem::exists(direct)) return direct;
  for (const char* base :
       {"variants", "../variants", "../../variants", "share/chessbox/variants"}) {
    const std::filesystem::path p = std::filesystem::path(base) / (name + ".toml");
    if (std::filesystem::exists(p)) return p;
  }
  return std::filesystem::path("variants") / (name + ".toml");
}

void printHelp() {
  std::printf(
      "chessbox-gui [variant]\n\n"
      "  left click      select a piece, then click a highlighted cell to move\n"
      "  right drag      orbit the camera\n"
      "  wheel           zoom\n"
      "  u               undo        r  reset\n"
      "  1-9             screen-axis presets (boards with more than two axes)\n"
      "  q / Esc         quit\n");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string variantName = argc > 1 ? argv[1] : "standard";
  if (variantName == "-h" || variantName == "--help") {
    printHelp();
    return 0;
  }

  auto variant = loadVariantFile(variantPath(variantName));
  if (!variant.has_value()) {
    std::fprintf(stderr, "cannot load variant '%s': %s\n", variantName.c_str(),
                 variant.error().format().c_str());
    return 1;
  }
  auto session = app::Session::create(std::move(*variant));
  if (!session.has_value()) {
    std::fprintf(stderr, "cannot start: %s\n", session.error().format().c_str());
    return 1;
  }
  app::Session& s = **session;

  auto window = render::Window::create(("ChessBox - " + variantName).c_str(), 1280, 800);
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

  printHelp();
  std::printf("\n%s\n", s.statusLine().c_str());

  bool running = true;
  bool orbiting = false;
  std::string lastStatus;

  while (running) {
    SDL_Event e;
    // Block until something happens: a board game has nothing to animate, and spinning
    // the GPU to redraw an unchanged position would just heat the room.
    if (!SDL_WaitEvent(&e)) break;
    do {
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
          if (e.button.button == SDL_BUTTON_LEFT) {
            s.clickPixel(e.button.x, e.button.y, static_cast<float>(window->width()),
                         static_cast<float>(window->height()));
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
            (void)s.apply(a);
          }
          break;
        case SDL_EVENT_MOUSE_WHEEL: {
          app::Action a;
          a.kind = app::ActionKind::Zoom;
          a.dx = e.wheel.y > 0 ? 0.9f : 1.1f;
          (void)s.apply(a);
          break;
        }
        case SDL_EVENT_KEY_DOWN: {
          app::Action a;
          switch (e.key.key) {
            case SDLK_Q:
            case SDLK_ESCAPE:
              running = false;
              break;
            case SDLK_U:
              a.kind = app::ActionKind::Undo;
              (void)s.apply(a);
              break;
            case SDLK_R:
              a.kind = app::ActionKind::Reset;
              (void)s.apply(a);
              break;
            default:
              // Number keys pick which axes are drawn spatially, which is how a
              // higher-dimensional board is inspected from another angle.
              if (e.key.key >= SDLK_1 && e.key.key <= SDLK_9) {
                const auto pick = static_cast<std::uint8_t>(e.key.key - SDLK_1);
                const DimSpec& d = s.variant().dims;
                if (pick + 1 < d.dims()) {
                  a.kind = app::ActionKind::SetScreenAxes;
                  a.text =
                      d.name(pick) + "," + d.name(static_cast<std::size_t>(pick) + 1);
                  if (auto ok = s.apply(a); !ok.has_value()) {
                    std::printf("%s\n", ok.error().format().c_str());
                  }
                }
              }
              break;
          }
          break;
        }
        default:
          break;
      }
    } while (SDL_PollEvent(&e));

    if (s.statusLine() != lastStatus) {
      lastStatus = s.statusLine();
      std::printf("%s\n", lastStatus.c_str());
      std::fflush(stdout);
    }

    const auto instances = renderer->buildInstances(s.snapshot(), s.viewConfig());
    if (auto ok = renderer->render(*target, instances, s.camera()); !ok.has_value()) {
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
