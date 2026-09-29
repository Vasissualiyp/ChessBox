// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/session.hpp"
#include "render/vulkan_context.hpp"
#include "view/theme.hpp"

struct SDL_Window;
union SDL_Event;

namespace cb::render {

/// What the interface is asking the application to do next.
///
/// The UI decides nothing itself - it reports intent and the caller acts - which keeps
/// the rule "the renderer never invents a move" true of the panels as well as the board.
struct UiRequest {
  bool quit{false};
  /// Non-empty when the player picked a different variant from the library.
  std::string loadVariant;
  /// The area the rails leave free, as x, y, width, height in pixels. The board is
  /// drawn and picked in this rectangle, so it is centred in what the player can
  /// actually see rather than behind a panel.
  float boardRect[4]{0, 0, 0, 0};
};

/// The game's interface: rails, ledger, status, promotion, library.
///
/// Drawn with Dear ImGui, restyled until it stops looking like Dear ImGui - the
/// defaults are tool-grey with soft rounding, and this is a board game played by
/// candlelight. Colours come from view::Theme, so retheming the game, or letting a
/// variant carry its own palette, never means touching this file.
class Ui {
 public:
  static Result<std::unique_ptr<Ui>> create(const VulkanContext& ctx, SDL_Window* window,
                                            const view::Theme& theme);
  ~Ui();
  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;

  /// Feed a platform event. Returns true when the interface consumed it, in which case
  /// the click was on a panel and must not also fall through to the board.
  bool processEvent(const SDL_Event& e);

  /// True while the pointer or the keyboard belongs to the interface.
  [[nodiscard]] bool capturesMouse() const;
  [[nodiscard]] bool capturesKeyboard() const;

  void newFrame();
  /// Build the whole interface for this frame.
  UiRequest build(app::Session& session, const std::vector<std::string>& library,
                  const std::string& currentVariant, float fps);
  /// Finish the frame and record its draw commands into the board's render pass.
  void endFrame();
  void record(VkCommandBuffer cmd);

 private:
  Ui() = default;
  void applyStyle();
  Result<void> loadFonts();

  const VulkanContext* ctx_{nullptr};
  SDL_Window* window_{nullptr};
  view::Theme theme_{};
  VkDescriptorPool pool_{VK_NULL_HANDLE};
  void* fontDisplay_{nullptr};  ///< ImFont*, kept opaque so the header stays light
  void* fontBody_{nullptr};
  void* fontSmall_{nullptr};
  bool initialised_{false};
};

}  // namespace cb::render
