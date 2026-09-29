// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "game/game.hpp"
#include "view/camera.hpp"
#include "view/layout.hpp"
#include "view/snapshot.hpp"

namespace cb::app {

enum class ActionKind : std::uint8_t {
  None,
  ClickCell,     ///< the user clicked a cell (already resolved from a pixel)
  Undo,
  Reset,
  Orbit,         ///< dx, dy in radians
  Zoom,          ///< dx as a multiplier
  SetScreenAxes, ///< text is a comma-separated axis name list
  SetPromotion,  ///< text is a piece name
};

struct Action {
  ActionKind kind{ActionKind::None};
  CellId cell{kInvalidCell};
  float dx{0};
  float dy{0};
  std::string text;
};

/// Everything a front end needs, with no front end in it.
///
/// Input arrives as actions and the result is a snapshot; nothing here knows about SDL,
/// Vulkan, or a window. That is what lets a scripted action log drive the whole
/// interaction path in a test - selection, illegal clicks, promotion, undo - with no
/// display and no screenshots (M4.6).
class Session {
 public:
  /// Takes the variant by value and keeps it at a stable address: Game, Position and
  /// MoveGen all hold pointers into it.
  static Result<std::unique_ptr<Session>> create(VariantSpec variant);

  [[nodiscard]] const VariantSpec& variant() const noexcept { return *variant_; }
  [[nodiscard]] const Game& game() const noexcept { return *game_; }
  [[nodiscard]] const view::PositionView& snapshot() const noexcept { return snapshot_; }
  [[nodiscard]] const view::ViewConfig& viewConfig() const noexcept { return viewCfg_; }
  [[nodiscard]] const view::OrbitCamera& camera() const noexcept { return camera_; }
  [[nodiscard]] const std::vector<view::Placement>& placements() const noexcept {
    return placements_;
  }
  [[nodiscard]] CellId selected() const noexcept { return selected_; }
  /// The last thing that happened, in words, for a status bar.
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] std::string statusLine() const;

  Result<void> apply(const Action& a);

  /// Replace the current position from FEN-N, discarding history. Used for setting up a
  /// study or a test position, and by the front end's position entry.
  Result<void> loadFen(std::string_view fen);

  /// Resolve a pixel to a cell and act on it. Returns kInvalidCell if the click missed
  /// the board, which is not an error - it deselects, like clicking off a board does.
  CellId clickPixel(float px, float py, float width, float height);

  /// Run a newline-separated action script. Written for tests, and equally the basis of
  /// a replay format.
  Result<void> applyScript(std::string_view script);

 private:
  Session() = default;
  void refreshSnapshot();
  void refreshView();
  [[nodiscard]] const Move* findMove(CellId from, CellId to) const;

  std::unique_ptr<VariantSpec> variant_;
  std::unique_ptr<Game> game_;
  view::PositionView snapshot_;
  view::ViewConfig viewCfg_;
  view::OrbitCamera camera_;
  std::vector<view::Placement> placements_;
  CellId selected_{kInvalidCell};
  std::string promotionPreference_;
  std::string message_;
};

/// Parse one line of an action script: "click e4", "undo", "reset", "orbit 0.1 0",
/// "zoom 1.2", "axes file,rank,level", "promote queen".
Result<Action> parseAction(const VariantSpec& v, std::string_view line);

}  // namespace cb::app
