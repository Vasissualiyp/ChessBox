// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/editor.hpp"
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
  Editor,       ///< board and piece designers, not built yet
  GameInfo,     ///< what the loaded variant's rules actually are
  PieceMoves,   ///< how each piece in the loaded variant moves
  QuitConfirm,  ///< the "are you sure?" step before the window closes
};

std::string_view screenName(Screen s) noexcept;

/// How far in from the front door a screen is.
///
/// The shell uses it for two things at once: the camera pushes forward when the depth
/// increases and back when it decreases, and the decoration changes sides on every
/// step. Both fall out of this number, so neither needs per-screen authoring.
int screenDepth(Screen s) noexcept;

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
  /// Install the loader and give the library its final order: difficulty first, then
  /// name. The order can only be computed here, because a difficulty lives in the
  /// variant's own file rather than in the name list the shell is handed at startup.
  void setVariantLoader(VariantLoader loader);
  Result<void> startGame(const std::string& variantName);

  /// A variant's advertised difficulty, for the library's order and its colours.
  /// Resolved through the loader once and remembered; a name that will not load is
  /// `Other` rather than an error, because the picker must still be able to draw it.
  [[nodiscard]] Difficulty difficultyOf(const std::string& name) const;

  /// The one-line description a variant's own file gives it, or empty when it cannot be
  /// loaded. The picker has only names to work from, so this is how it shows an author's
  /// pitch before the game itself is started.
  [[nodiscard]] std::string variantDescription(const std::string& name) const;

  /// The spec of a variant the player is *looking at* but has not started.
  ///
  /// The library screen draws the lattice of whichever variant is selected, which means
  /// it needs a loaded spec for a game that does not exist yet. One is cached, because
  /// the alternative is parsing a file every frame; a name that will not load gives
  /// nullptr and the screen falls back to a plain board.
  [[nodiscard]] const VariantSpec* preview(const std::string& name) const;

  /// The variant's *source text*, for the editor. Separate from the resolved-spec loader
  /// above because editing needs the author's spelling, which the spec has thrown away;
  /// injected like the loader so the shell stays testable without files.
  using VariantSourceLoader = std::function<Result<std::string>(const std::string&)>;
  void setVariantSourceLoader(VariantSourceLoader loader) {
    sourceLoader_ = std::move(loader);
  }

  /// Where a variant's file is written when the editor saves. Injected for the same
  /// reason as the loaders: the shell stays testable without a filesystem.
  using VariantPathResolver = std::function<std::filesystem::path(const std::string&)>;
  void setVariantPathResolver(VariantPathResolver resolver) {
    pathResolver_ = std::move(resolver);
  }

  /// Open a variant's source in the editor. Fails when no source loader is installed or
  /// the variant will not load. Replaces any document already open.
  Result<void> openEditor(const std::string& variantName);

  /// Write the open document back to its file. Fails when nothing is open or no path
  /// resolver is installed.
  Result<void> saveEditor();

  /// The document being edited, or nullptr when the editor has nothing open.
  [[nodiscard]] Editor* editor() noexcept { return editor_.get(); }
  [[nodiscard]] const Editor* editor() const noexcept { return editor_.get(); }
  [[nodiscard]] const std::string& editorVariant() const noexcept {
    return editorVariant_;
  }

  void pause();
  void resume();

  /// Persist settings, and apply anything that has to reach the session.
  void applySettings();

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
  bool quit_{false};
  VariantLoader loader_;
  VariantSourceLoader sourceLoader_;
  VariantPathResolver pathResolver_;
  std::unique_ptr<Editor> editor_;
  std::string editorVariant_;
  std::filesystem::path editorPath_;
  mutable std::unique_ptr<VariantSpec> preview_;
  mutable std::string previewName_;
  /// Difficulty per variant name, filled on first ask so the picker does not re-read a
  /// file for every row on every frame.
  mutable std::map<std::string, Difficulty> difficulty_;
};

}  // namespace cb::app
