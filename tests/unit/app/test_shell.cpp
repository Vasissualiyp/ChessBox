// SPDX-License-Identifier: GPL-3.0-or-later
// The application around the game: which screens a player can reach, and what happens
// to a game while they are elsewhere. Tested without a window, because these are the
// transitions that break quietly - resuming with nothing loaded, losing a position by
// opening the settings, a menu offering something that does not exist.
#include "app/shell.hpp"

#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::unique_ptr<Shell> makeShell(const std::filesystem::path& settings = {}) {
  auto shell = Shell::create({"standard", "klein", "cube5"}, settings);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  return shell;
}

std::filesystem::path tempSettings(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "chessbox-tests";
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::filesystem::remove(path);
  return path;
}

}  // namespace

TEST_CASE("the game opens on the main menu with nothing loaded", "[unit][app]") {
  auto shell = makeShell(tempSettings("open.conf"));
  REQUIRE(shell->screen() == Screen::MainMenu);
  REQUIRE_FALSE(shell->hasGame());
  REQUIRE_FALSE(shell->showsBoard());
  REQUIRE_FALSE(shell->quitRequested());
}

TEST_CASE("starting a game puts you in it", "[unit][app]") {
  auto shell = makeShell(tempSettings("start.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  REQUIRE(shell->screen() == Screen::Game);
  REQUIRE(shell->hasGame());
  REQUIRE(shell->showsBoard());
  REQUIRE(shell->currentVariant() == "standard");
  REQUIRE(shell->session()->game().legalMoves().size() == 20);
}

TEST_CASE("a variant that will not load leaves the shell usable", "[unit][app]") {
  auto shell = makeShell(tempSettings("bad.conf"));
  REQUIRE_FALSE(shell->startGame("no-such-variant").has_value());
  REQUIRE(shell->screen() == Screen::MainMenu);
  REQUIRE_FALSE(shell->hasGame());
  REQUIRE_FALSE(shell->message().empty());  // and it says why
}

TEST_CASE("the picker reads a variant's own description", "[unit][app]") {
  // The picker knows only names, so the shell is what turns one into the line the
  // variant file carries. A name that will not load yields nothing rather than an
  // invented sentence.
  auto shell = makeShell(tempSettings("describe.conf"));
  REQUIRE_FALSE(shell->variantDescription("klein").empty());
  REQUIRE(shell->variantDescription("no-such-variant").empty());
}

TEST_CASE("pausing keeps the game and the board", "[unit][app]") {
  // The pause screen is an overlay: losing sight of the position while paused would
  // make "gamemode info" and "piece moves" much less useful than they should be.
  auto shell = makeShell(tempSettings("pause.conf"));
  REQUIRE(shell->startGame("standard").has_value());
  REQUIRE(shell->session()->applyScript("click e2\nclick e4").has_value());

  shell->pause();
  REQUIRE(shell->screen() == Screen::Paused);
  REQUIRE(shell->showsBoard());
  REQUIRE(shell->session()->game().plyCount() == 1);

  shell->go(Screen::PieceMoves);
  REQUIRE(shell->showsBoard());
  shell->back();
  REQUIRE(shell->screen() == Screen::Paused);

  shell->resume();
  REQUIRE(shell->screen() == Screen::Game);
  REQUIRE(shell->session()->game().plyCount() == 1);  // the position survived
}

TEST_CASE("pausing with no game loaded does nothing", "[unit][app]") {
  auto shell = makeShell(tempSettings("nopause.conf"));
  shell->pause();
  REQUIRE(shell->screen() == Screen::MainMenu);
  shell->resume();
  REQUIRE(shell->screen() == Screen::MainMenu);
}

TEST_CASE("settings return you where you came from", "[unit][app]") {
  auto shell = makeShell(tempSettings("return.conf"));

  SECTION("from the main menu") {
    shell->go(Screen::Settings);
    REQUIRE_FALSE(shell->showsBoard());
    shell->back();
    REQUIRE(shell->screen() == Screen::MainMenu);
  }

  SECTION("from a paused game") {
    REQUIRE(shell->startGame("standard").has_value());
    shell->pause();
    shell->go(Screen::Settings);
    // Opened over a game, the board stays visible behind the panel.
    REQUIRE(shell->showsBoard());
    shell->back();
    REQUIRE(shell->screen() == Screen::Paused);
    REQUIRE(shell->hasGame());
  }
}

TEST_CASE("unbuilt menu entries say what they will be", "[unit][app]") {
  // A menu that hides what is coming is dishonest; one that pretends a feature works is
  // worse. Each unbuilt entry names itself and explains where the game actually stands.
  auto shell = makeShell(tempSettings("unbuilt.conf"));
  for (Unbuilt f : {Unbuilt::Continue, Unbuilt::Multiplayer, Unbuilt::BoardDesigner,
                    Unbuilt::PieceDesigner}) {
    shell->noteUnbuilt(f);
    REQUIRE(shell->lastUnbuiltAttempt() == f);
    REQUIRE_FALSE(unbuiltName(f).empty());
    REQUIRE(unbuiltNote(f).size() > 40);
  }
  shell->clearUnbuilt();
  REQUIRE(shell->lastUnbuiltAttempt() == Unbuilt::None);
  // Changing screen clears it, so a note never follows you somewhere it makes no sense.
  shell->noteUnbuilt(Unbuilt::Multiplayer);
  shell->go(Screen::Settings);
  REQUIRE(shell->lastUnbuiltAttempt() == Unbuilt::None);
}

TEST_CASE("settings survive a restart", "[unit][app]") {
  const auto path = tempSettings("roundtrip.conf");
  {
    auto shell = makeShell(path);
    shell->settings().guiScale = 1.6f;
    shell->settings().showSeams = false;
    shell->settings().invertOrbitY = true;
    shell->settings().autoPromoteTo = "knight";
    shell->settings().volumeMusic = 0.25f;
    REQUIRE(shell->startGame("klein").has_value());  // also records the last variant
    shell->applySettings();
  }
  {
    auto shell = makeShell(path);
    REQUIRE(shell->settings().guiScale == 1.6f);
    REQUIRE_FALSE(shell->settings().showSeams);
    REQUIRE(shell->settings().invertOrbitY);
    REQUIRE(shell->settings().autoPromoteTo == "knight");
    REQUIRE(shell->settings().volumeMusic == 0.25f);
    REQUIRE(shell->currentVariant() == "klein");
  }
}

TEST_CASE("a broken settings file does not stop the game starting", "[unit][app]") {
  // The file is hand-editable and may come from a newer build, so anything unparseable
  // has to degrade to a default rather than take the game down with it.
  const auto path = tempSettings("broken.conf");
  {
    std::ofstream out(path);
    out << "this line has no equals sign\n";
    out << "gui_scale = not-a-number\n";
    out << "gui_scale = 1.25\n";
    out << "unknown_future_key = 7\n";
    out << "# a comment\n";
    out << "show_seams = false   # trailing comment\n";
  }
  const Settings s = Settings::load(path);
  REQUIRE(s.guiScale == 1.25f);
  REQUIRE_FALSE(s.showSeams);
  REQUIRE(s.showLegalMoves);  // untouched keys keep their defaults
}

TEST_CASE("settings are clamped to usable values", "[unit][app]") {
  Settings s;
  s.guiScale = 99.0f;
  s.orbitSensitivity = -4.0f;
  s.volumeMaster = 5.0f;
  s.lastVariant.clear();
  s.sanitize();
  REQUIRE(s.guiScale <= 3.0f);
  REQUIRE(s.orbitSensitivity >= 0.1f);
  REQUIRE(s.volumeMaster <= 1.0f);
  REQUIRE_FALSE(s.lastVariant.empty());
}

TEST_CASE("a missing settings file means defaults, not an error", "[unit][app]") {
  const Settings s = Settings::load("/nonexistent/path/settings.conf");
  REQUIRE(s.guiScale == 1.0f);
  REQUIRE(s.lastVariant == "standard");
}
