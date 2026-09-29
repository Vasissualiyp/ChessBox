// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/session.hpp"
#include "app/shell.hpp"
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
  /// A setting changed and should be written to disk.
  bool settingsChanged{false};
  /// The interface scale changed; fonts have to be rebuilt at the new size.
  bool applyScale{false};
  bool toggleFullscreen{false};
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

  /// Build whichever screen the shell says the player is on.
  UiRequest build(app::Shell& shell, float fps);

  /// Rebuild the fonts at a new interface scale.
  ///
  /// The atlas is regenerated rather than the existing glyphs stretched: a scale option
  /// that produces blurry text is not worth having. Must not be called inside a frame.
  Result<void> setScale(float scale);
  /// Finish the frame and record its draw commands into the board's render pass.
  void endFrame();
  void record(VkCommandBuffer cmd);

 private:
  Ui() = default;
  void applyStyle();
  Result<void> loadFonts();

  UiRequest buildGameHud(app::Shell& shell, float fps);
  /// Pixels at the current interface scale. Every hardcoded size goes through this, or
  /// the scale option moves the text and leaves the panels behind.
  [[nodiscard]] float px(float v) const noexcept { return v * scale_; }
  UiRequest buildMainMenu(app::Shell& shell);
  UiRequest buildNewGame(app::Shell& shell);
  UiRequest buildPause(app::Shell& shell);
  UiRequest buildSettings(app::Shell& shell);
  UiRequest buildCreator(app::Shell& shell);
  UiRequest buildGameInfo(app::Shell& shell);
  UiRequest buildPieceMoves(app::Shell& shell);

  const VulkanContext* ctx_{nullptr};
  SDL_Window* window_{nullptr};
  view::Theme theme_{};
  VkDescriptorPool pool_{VK_NULL_HANDLE};
  void* fontDisplay_{nullptr};  ///< ImFont*, kept opaque so the header stays light
  void* fontBody_{nullptr};
  void* fontSmall_{nullptr};
  float scale_{1.0f};
  /// Which variant the new-game screen is showing details for.
  std::string pickedVariant_;
  /// Which pair of axes the "axes" control will show next.
  int axisRotation_{0};
  bool initialised_{false};
};

}  // namespace cb::render
