// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/shell.hpp"

#include "io/variant_toml.hpp"

namespace cb::app {

std::string_view screenName(Screen s) noexcept {
  switch (s) {
    case Screen::MainMenu:
      return "main menu";
    case Screen::NewGame:
      return "new game";
    case Screen::Game:
      return "game";
    case Screen::Paused:
      return "paused";
    case Screen::Settings:
      return "settings";
    case Screen::Creator:
      return "creator";
    case Screen::GameInfo:
      return "game mode";
    case Screen::PieceMoves:
      return "piece moves";
  }
  return "?";
}

std::string_view unbuiltName(Unbuilt f) noexcept {
  switch (f) {
    case Unbuilt::None:
      return "";
    case Unbuilt::Continue:
      return "Continue";
    case Unbuilt::Multiplayer:
      return "Multiplayer";
    case Unbuilt::BoardDesigner:
      return "Board designer";
    case Unbuilt::PieceDesigner:
      return "Piece designer";
  }
  return "";
}

std::string_view unbuiltNote(Unbuilt f) noexcept {
  switch (f) {
    case Unbuilt::None:
      return "";
    case Unbuilt::Continue:
      return "Saved games are not written yet, so there is nothing to continue. Start a "
             "new game from the library instead.";
    case Unbuilt::Multiplayer:
      return "The engine is already the server - the same rules decide every move - but "
             "nothing sends them over a wire yet.";
    case Unbuilt::BoardDesigner:
      return "Boards are data today: a variant file declares its axes, its geometry and "
             "its opening position. The designer will edit that file for you.";
    case Unbuilt::PieceDesigner:
      return "A piece is a set of vector-move atoms, so the designer is a way to draw "
             "one and see the directions it expands to. Read them in Piece moves for "
             "now.";
  }
  return "";
}

std::unique_ptr<Shell> Shell::create(std::vector<std::string> library,
                                     std::filesystem::path settingsPath) {
  auto shell = std::unique_ptr<Shell>(new Shell());
  shell->library_ = std::move(library);
  shell->settingsPath_ =
      settingsPath.empty() ? Settings::defaultPath() : std::move(settingsPath);
  shell->settings_ = Settings::load(shell->settingsPath_);
  shell->currentVariant_ = shell->settings_.lastVariant;
  return shell;
}

bool Shell::showsBoard() const noexcept {
  // The pause and reference screens are overlays: seeing the position behind them is
  // most of the reason to open them at all.
  switch (screen_) {
    case Screen::Game:
    case Screen::Paused:
    case Screen::GameInfo:
    case Screen::PieceMoves:
      return session_ != nullptr;
    case Screen::Settings:
      return session_ != nullptr && previous_ != Screen::MainMenu;
    default:
      return false;
  }
}

void Shell::go(Screen s) {
  if (s == screen_) return;
  // Only remember a screen worth returning to: bouncing between two overlays should
  // still take you back to the game.
  if (screen_ != Screen::Settings && screen_ != Screen::GameInfo &&
      screen_ != Screen::PieceMoves && screen_ != Screen::Creator) {
    previous_ = screen_;
  }
  screen_ = s;
  unbuilt_ = Unbuilt::None;
}

void Shell::back() {
  switch (screen_) {
    case Screen::Settings:
    case Screen::Creator:
    case Screen::NewGame:
      screen_ = previous_;
      break;
    case Screen::GameInfo:
    case Screen::PieceMoves:
      screen_ = session_ != nullptr ? Screen::Paused : Screen::MainMenu;
      break;
    case Screen::Paused:
      screen_ = Screen::Game;
      break;
    case Screen::Game:
      screen_ = Screen::Paused;
      break;
    case Screen::MainMenu:
      break;
  }
  unbuilt_ = Unbuilt::None;
}

Result<void> Shell::startGame(const std::string& variantName) {
  if (!loader_) return fail(ErrorCode::Internal, "no variant loader is installed");
  auto variant = loader_(variantName);
  if (!variant.has_value()) {
    message_ = variant.error().format();
    return fail(variant.error().code, variant.error().message);
  }
  auto session = Session::create(std::move(*variant));
  if (!session.has_value()) {
    message_ = session.error().format();
    return fail(session.error().code, session.error().message);
  }
  session_ = std::move(*session);
  currentVariant_ = variantName;
  settings_.lastVariant = variantName;
  message_.clear();
  applySettings();
  screen_ = Screen::Game;
  previous_ = Screen::MainMenu;
  return {};
}

void Shell::pause() {
  if (session_ != nullptr) go(Screen::Paused);
}

void Shell::resume() {
  if (session_ != nullptr) go(Screen::Game);
}

void Shell::applySettings() {
  settings_.sanitize();
  if (session_ != nullptr) {
    Action a;
    a.kind = ActionKind::SetPromotion;
    a.text = settings_.autoPromoteTo;
    // An empty preference means "always ask", which the session expresses by having no
    // preference at all rather than by a sentinel piece.
    if (!settings_.autoPromoteTo.empty()) (void)session_->apply(a);
  }
  if (auto ok = settings_.save(settingsPath_); !ok.has_value()) {
    message_ = ok.error().message;
  }
}

void Shell::noteUnbuilt(Unbuilt f) {
  unbuilt_ = f;
}

}  // namespace cb::app
