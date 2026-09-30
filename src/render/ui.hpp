// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/session.hpp"
#include "app/shell.hpp"
#include "render/deco.hpp"
#include "render/piece_icon.hpp"
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
  /// A purely visual setting changed - the icon set, say - and has to reach the
  /// interface rather than the engine.
  bool applyLooks{false};
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

  /// Advance the shell's own clock. The menus animate continuously - the manifold
  /// turns, the field drifts - so the interface needs time, which only the main loop
  /// knows about.
  void tick(float dt);

  void newFrame();

  /// Build whichever screen the shell says the player is on.
  UiRequest build(app::Shell& shell, float fps);

  /// Rebuild the fonts at a new interface scale.
  ///
  /// The atlas is regenerated rather than the existing glyphs stretched: a scale option
  /// that produces blurry text is not worth having. Must not be called inside a frame.
  Result<void> setScale(float scale);
  void setIconStyle(IconStyle s) noexcept { iconStyle_ = s; }
  /// Finish the frame and record its draw commands into the board's render pass.
  void endFrame();
  void record(VkCommandBuffer cmd);

 private:
  Ui() = default;
  void applyStyle();
  Result<void> loadFonts();

  UiRequest buildGameHud(app::Shell& shell, float fps);
  /// The two-pane frame every menu screen sits in: the drifting field, the screen's
  /// own object, and the depth ladder. Returns the rectangle the menu itself gets.
  void drawShellFrame(app::Shell& shell, ImVec2& menuMin, ImVec2& menuMax);
  /// The pause section's frame: the board keeps the left of the screen behind a scrim
  /// (the main loop draws it there, pulled back and blurred), and the menu pane takes
  /// the right. Returns where the pane goes and leaves the board rectangle on `request`.
  void pauseFrame(UiRequest& request, ImVec2& menuMin, ImVec2& menuMax);
  /// The piece designer's left half: a board with the selected piece at its centre and
  /// the cells it can reach marked quiet or capture. Reads the editor's atoms and walks
  /// them, so it shows the real movement rather than a picture of it.
  void drawEditorPreview(app::Shell& shell, const ImVec2& min, const ImVec2& max);
  /// The library's object: the selected variant's overture, in the half the
  /// decoration would otherwise occupy.
  void drawOvertureObject(app::Shell& shell, const ImVec2& min, const ImVec2& max,
                          float zoom, float alpha);
  /// Let the player turn whatever object the screen is showing, by dragging it.
  ///
  /// The turn is an *offset* on the object's own animation rather than a replacement, so
  /// a manifold someone has taken hold of keeps turning under their hand instead of
  /// freezing. Kept across screens, because it reads as one camera on one object.
  void updateObjectDrag(app::Shell& shell, const ImVec2& min, const ImVec2& max);
  /// A red glow at the screen's edges while the side to move is in check, on every
  /// screen: the position is in trouble whether or not the board is the thing showing.
  void drawCheckEdges(app::Shell& shell);

  /// Where a pane sits in the shell's transition, as the scale and fade the pane is
  /// drawn with. The arriving pane and the departing ghost ask for their own.
  struct PaneMove {
    float scale{1.0f};
    float alpha{1.0f};
  };
  [[nodiscard]] PaneMove paneMove() const noexcept;
  /// The two halves of the transition clock. The departing screen runs first and is
  /// finished before the arriving one begins, so the two moves are a sequence rather
  /// than a cross-fade; `outEase` reaches 1 while `inEase` is still 0.
  [[nodiscard]] float outEase() const noexcept;
  [[nodiscard]] float inEase() const noexcept;
  /// Draw the screen being left, once more, scaled and faded - the departing menu has to
  /// be seen to shrink or swell, not simply switch off. Its input is disabled and its
  /// request discarded, so re-running its build cannot start or change anything.
  void drawGhost(app::Shell& shell);
  /// Pixels at the current interface scale. Every hardcoded size goes through this, or
  /// the scale option moves the text and leaves the panels behind.
  [[nodiscard]] float px(float v) const noexcept { return v * scale_; }
  UiRequest buildMainMenu(app::Shell& shell);
  UiRequest buildNewGame(app::Shell& shell);
  UiRequest buildPause(app::Shell& shell);
  UiRequest buildSettings(app::Shell& shell);
  UiRequest buildEditor(app::Shell& shell);
  UiRequest buildQuitConfirm(app::Shell& shell);
  UiRequest buildPauseQuitConfirm(app::Shell& shell);
  UiRequest buildGameInfo(app::Shell& shell);
  UiRequest buildPieceMoves(app::Shell& shell);

  const VulkanContext* ctx_{nullptr};
  SDL_Window* window_{nullptr};
  view::Theme theme_{view::Theme::manifold()};
  VkDescriptorPool pool_{VK_NULL_HANDLE};
  void* fontDisplay_{nullptr};  ///< ImFont*, kept opaque so the header stays light
  void* fontBody_{nullptr};
  void* fontSmall_{nullptr};
  void* fontMono_{nullptr};
  /// Which flat-piece table to draw from. Pushed in from the player's settings; the
  /// interface holds no opinion of its own about it.
  IconStyle iconStyle_{IconStyle::Faceted};
  float scale_{1.0f};
  /// The shell's clock, and where it is in a screen change. `enter_` runs 0 to 1 as a
  /// screen arrives; the menu is scaled and faded along it, which is as close to a
  /// camera push as a 2-D draw list gets.
  float clock_{0.0f};
  /// The last frame's dt, so `build` can move the overture on. `tick` is the only place
  /// that knows about time, and the overture is advanced where the shell is to hand.
  float lastDt_{0.0f};
  float enter_{1.0f};
  int lastScreen_{-1};
  /// Which piece the piece designer has selected. Editor state, not shell state: the
  /// document is what matters and it lives in the shell; which row is highlighted is a
  /// view concern.
  int editorPiece_{0};
  /// The editor's own page: its home (pick a designer), the piece designer, or its help.
  /// Kept here rather than as a Screen so stepping in does not change the shell's depth.
  int editorPage_{0};

  /// The piece preview's board: the size, where it was drawn, and what the player has
  /// placed on it (0 empty, 1 black, 2 white). The selected piece is white and stands at
  /// the centre; the rest is a scratch board for trying captures.
  static constexpr int kPreviewN = 10;
  ImVec2 previewOrigin_{0.0f, 0.0f};
  float previewCell_{0.0f};
  bool previewValid_{false};
  std::array<std::array<std::uint8_t, kPreviewN>, kPreviewN> previewCells_{};
  /// The screen being left, kept as a number so its own build can be re-run as a ghost,
  /// and the screen being built now, so `paneMove` knows whether this is a menu move.
  int leavingScreen_{-1};
  int currentScreen_{-1};
  /// True only while drawGhost is building the departing pane, so drawShellFrame skips
  /// the background it already drew and beginPane uses the departing move.
  bool ghosting_{false};
  /// The half of the frame the object owns, remembered by drawShellFrame so a screen can
  /// put its own thing there: the piece reference draws its move diagrams, not a deco.
  ImVec2 decoMin_{0, 0};
  ImVec2 decoMax_{0, 0};
  /// Which piece the piece-moves reference shows on its left. View state, like the
  /// editor's selected piece.
  int pieceMovesPick_{0};
  /// How far the player has turned the menu's object, and whether they are turning it
  /// now. View state: which way an ornament faces is not something the shell decides.
  float objectYaw_{0.0f};
  float objectElev_{0.0f};
  bool objectDrag_{false};
  /// The decoration of the screen being left, and which way the camera is going. One
  /// clock (`enter_`) drives the arriving object, the leaving ghost, the menu pane and
  /// the field, so none of them can disagree about where the camera is.
  Deco leaving_{Deco::None};
  bool deeper_{true};
  DepthField field_;
  /// Which variant the new-game screen is showing details for.
  std::string pickedVariant_;
  /// That variant's own one-line description, and the name it was read for, so the file
  /// is read once per selection rather than once per frame.
  std::string pickedDescription_;
  std::string describedFor_;
  /// Which pair of axes the "axes" control will show next.
  int axisRotation_{0};
  bool initialised_{false};
};

}  // namespace cb::render
