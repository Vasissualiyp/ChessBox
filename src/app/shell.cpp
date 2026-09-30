// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/shell.hpp"

#include <algorithm>

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
    case Screen::Editor:
      return "editor";
    case Screen::GameInfo:
      return "game mode";
    case Screen::PieceMoves:
      return "piece moves";
    case Screen::QuitConfirm:
    case Screen::PauseQuitConfirm:
      return "quit";
  }
  return "?";
}

int screenDepth(Screen s) noexcept {
  switch (s) {
    case Screen::MainMenu:
      return 0;
    case Screen::NewGame:
    case Screen::Settings:
    case Screen::Editor:
    case Screen::QuitConfirm:
      return 1;
    case Screen::Game:
      return 2;
    // Pause and the reference screens are one rung off the game, still in its context
    // rather than out at the shell's own menus.
    case Screen::Paused:
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return 2;
  }
  return 0;
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
  // The pause and reference screens sit over the position: the board stays behind them,
  // pulled back and blurred, which is the whole look of stepping away from a game.
  switch (screen_) {
    case Screen::Game:
    case Screen::Paused:
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return session_ != nullptr;
    case Screen::Settings:
      return session_ != nullptr && previous_ != Screen::MainMenu;
    default:
      return false;
  }
}

float Shell::boardPullBack() const noexcept {
  if (session_ == nullptr) return 0.0f;
  switch (screen_) {
    case Screen::Game:
      return 0.0f;
    case Screen::Paused:
      return 1.0f;
    case Screen::GameInfo:
    case Screen::PieceMoves:
    case Screen::PauseQuitConfirm:
      return 2.0f;
    case Screen::Settings:
      return previous_ != Screen::MainMenu ? 2.0f : 0.0f;
    default:
      return 0.0f;
  }
}

void Shell::go(Screen s) {
  if (s == screen_) return;
  // Opening the editor with nothing loaded edits the current variant, or standard as a
  // base. A failure leaves the screen with no document, which the screen reports rather
  // than trapping the player on a menu.
  if (s == Screen::Editor && editor_ == nullptr) {
    (void)openEditor(currentVariant_.empty() ? "standard" : currentVariant_);
  }
  // Only remember a screen worth returning to: bouncing between two overlays should
  // still take you back to the game.
  if (screen_ != Screen::Settings && screen_ != Screen::GameInfo &&
      screen_ != Screen::PieceMoves && screen_ != Screen::Editor) {
    previous_ = screen_;
  }
  screen_ = s;
}

void Shell::back() {
  switch (screen_) {
    case Screen::Settings:
    case Screen::Editor:
    case Screen::NewGame:
    case Screen::QuitConfirm:
    case Screen::PauseQuitConfirm:
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
}

void Shell::setVariantLoader(VariantLoader loader) {
  loader_ = std::move(loader);
  difficulty_.clear();
  // Difficulty first, then name. Ties are alphabetical, so the order is stable and
  // predictable rather than whatever the filesystem handed over.
  std::stable_sort(library_.begin(), library_.end(),
                   [this](const std::string& a, const std::string& b) {
                     const Difficulty da = difficultyOf(a);
                     const Difficulty db = difficultyOf(b);
                     if (da != db) return static_cast<int>(da) < static_cast<int>(db);
                     return a < b;
                   });
}

Difficulty Shell::difficultyOf(const std::string& name) const {
  // A running game already holds the spec, and it is the same one.
  if (session_ != nullptr && currentVariant_ == name)
    return session_->variant().difficulty;
  if (const auto it = difficulty_.find(name); it != difficulty_.end()) return it->second;
  Difficulty d = Difficulty::Other;
  if (loader_) {
    auto v = loader_(name);
    if (v.has_value()) d = v->difficulty;
  }
  difficulty_.emplace(name, d);
  return d;
}

Result<void> Shell::openEditor(const std::string& variantName) {
  if (!sourceLoader_) {
    return fail(ErrorCode::Internal, "no variant source loader is installed");
  }
  auto text = sourceLoader_(variantName);
  if (!text.has_value()) return fail(text.error().code, text.error().message);
  auto editor = Editor::open(*text, variantName);
  if (!editor.has_value()) {
    return fail(editor.error().code, editor.error().message, editor.error().line);
  }
  editor_ = std::make_unique<Editor>(std::move(*editor));
  editorVariant_ = variantName;
  editorPath_ = pathResolver_ ? pathResolver_(variantName) : std::filesystem::path{};
  return {};
}

Result<void> Shell::saveEditor() {
  if (editor_ == nullptr) {
    return fail(ErrorCode::Internal, "nothing is open in the editor");
  }
  if (editorPath_.empty()) {
    return fail(ErrorCode::Internal, "no save path is installed for the editor");
  }
  return editor_->saveFile(editorPath_);
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

std::string Shell::variantDescription(const std::string& name) const {
  // Asking the running session first saves reloading a file the game already holds.
  if (session_ != nullptr && currentVariant_ == name) {
    return session_->variant().description;
  }
  if (!loader_) return {};
  auto v = loader_(name);
  return v.has_value() ? v->description : std::string{};
}

const VariantSpec* Shell::preview(const std::string& name) const {
  if (name.empty()) return nullptr;
  // A running game already holds the spec, and it is the same one.
  if (session_ != nullptr && currentVariant_ == name) return &session_->variant();
  if (previewName_ == name) return preview_.get();
  if (!loader_) return nullptr;
  auto v = loader_(name);
  previewName_ = name;
  preview_ = v.has_value() ? std::make_unique<VariantSpec>(std::move(*v)) : nullptr;
  return preview_.get();
}

void Shell::resume() {
  if (session_ != nullptr) go(Screen::Game);
}

void Shell::applySettings() {
  settings_.sanitize();
  if (session_ != nullptr) {
    session_->setFlatView(settings_.flatView);
    session_->setHotSeat(settings_.hotSeat);
    // Seconds per cell travelled. Nought-point-nought-eight-five at speed 1 is about
    // as fast as a move can be read; switching animation off means zero, not "instant",
    // so the session has one rule for "do not animate" rather than two.
    session_->setAnimationSeconds(
        settings_.animateMoves ? 0.085f / settings_.animationSpeed : 0.0f);
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

}  // namespace cb::app
