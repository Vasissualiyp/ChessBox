// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/session.hpp"
#include "app/settings.hpp"

namespace cb::app {

/// Which screen the player is looking at.
enum class Screen : std::uint8_t {
  MainMenu,
  NewGame,  ///< choosing a variant
  Game,
  Paused,
  Settings,
  Creator,     ///< board and piece designers, not built yet
  GameInfo,    ///< what the loaded variant's rules actually are
  PieceMoves,  ///< how each piece in the loaded variant moves
};

std::string_view screenName(Screen s) noexcept;

/// Parts of the game that are announced but not built. Naming them in one place keeps
/// the menus honest: a disabled entry says what it will be, rather than pretending the
/// feature is a click away or hiding it until it exists.
enum class Unbuilt : std::uint8_t {
  None,
  Continue,
  Multiplayer,
  BoardDesigner,
  PieceDesigner,
};

std::string_view unbuiltName(Unbuilt f) noexcept;
/// One sentence on what the thing will do and, honestly, when.
std::string_view unbuiltNote(Unbuilt f) noexcept;

/// The application around the game: menus, settings, and the game itself.
///
/// Screen state and settings live here rather than in the interface, so the transitions
/// a player can make - and the ones they cannot, like resuming with no game loaded -
/// are testable without a window.
class Shell {
 public:
  static std::unique_ptr<Shell> create(std::vector<std::string> library,
                                       std::filesystem::path settingsPath = {});

  [[nodiscard]] Screen screen() const noexcept { return screen_; }
  [[nodiscard]] Settings& settings() noexcept { return settings_; }
  [[nodiscard]] const Settings& settings() const noexcept { return settings_; }
  [[nodiscard]] const std::vector<std::string>& library() const noexcept {
    return library_;
  }
  [[nodiscard]] const std::string& currentVariant() const noexcept {
    return currentVariant_;
  }
  [[nodiscard]] const std::string& message() const noexcept { return message_; }
  [[nodiscard]] bool quitRequested() const noexcept { return quit_; }

  /// The running game, or nullptr on a screen that has none.
  [[nodiscard]] Session* session() noexcept { return session_.get(); }
  [[nodiscard]] const Session* session() const noexcept { return session_.get(); }
  [[nodiscard]] bool hasGame() const noexcept { return session_ != nullptr; }

  /// True while the board should be drawn behind whatever screen is showing - the
  /// pause and reference screens sit over the game rather than replacing it.
  [[nodiscard]] bool showsBoard() const noexcept;

  void go(Screen s);
  void back();
  void requestQuit() { quit_ = true; }

  /// Start a game. The loader is injected so the shell stays testable without files.
  using VariantLoader = std::function<Result<VariantSpec>(const std::string&)>;
  void setVariantLoader(VariantLoader loader) { loader_ = std::move(loader); }
  Result<void> startGame(const std::string& variantName);

  /// The one-line description a variant's own file gives it, or empty when it cannot be
  /// loaded. The picker has only names to work from, so this is how it shows an author's
  /// pitch before the game itself is started.
  [[nodiscard]] std::string variantDescription(const std::string& name) const;

  void pause();
  void resume();

  /// Persist settings, and apply anything that has to reach the session.
  void applySettings();

  [[nodiscard]] Unbuilt lastUnbuiltAttempt() const noexcept { return unbuilt_; }
  void noteUnbuilt(Unbuilt f);
  void clearUnbuilt() { unbuilt_ = Unbuilt::None; }

 private:
  Shell() = default;

  std::vector<std::string> library_;
  std::filesystem::path settingsPath_;
  Settings settings_;
  std::unique_ptr<Session> session_;
  std::string currentVariant_;
  std::string message_;
  Screen screen_{Screen::MainMenu};
  Screen previous_{Screen::MainMenu};
  Unbuilt unbuilt_{Unbuilt::None};
  bool quit_{false};
  VariantLoader loader_;
};

}  // namespace cb::app
