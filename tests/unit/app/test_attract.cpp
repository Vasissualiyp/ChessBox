// SPDX-License-Identifier: GPL-3.0-or-later
//
// M13.9: the main-menu attract sequence. A looping run of short games, one per geometry
// beat, driven through the same watch machinery a bundled showcase uses. The tests are
// headless - they drive a Shell and some floats, never a window - and their job is the
// state machine: does every beat load, do its moves replay legally, does the sequence
// wrap forever, and does the main-menu board/decoration state flip without leaking to
// any other screen.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "app/shell.hpp"
#include "game/game.hpp"
#include "io/fen.hpp"
#include "io/game_file.hpp"
#include "position/position.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::unique_ptr<Shell> makeShell(const std::filesystem::path& settings = {}) {
  auto shell = Shell::create(
      {"standard", "cylinder", "torus", "klein", "torus3d_8x8", "hyper4"}, settings);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(name));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  shell->dismissWelcome();
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

TEST_CASE("every attract beat is a real, legal game for its variant",
          "[unit][app][attract]") {
  // The spec's check: a `GameFile` that fails to load or replay is a build-time failure,
  // not something to hand-wave past. `applyGameFile` is the engine's own legality proof.
  const AttractSequence seq = defaultAttractSequence();
  REQUIRE(seq.size() == 6);
  // The documented M18.6 workaround: open on the real standard board, then cut into the
  // cylinder roll in motion rather than at the rank-mirrored flat end.
  REQUIRE(seq.front().variant == "standard");
  REQUIRE(seq[1].variant == "cylinder");
  REQUIRE(seq[1].formedFrom > 0.0f);
  for (const AttractStep& step : seq) {
    CAPTURE(step.variant);
    const VariantSpec v = test::loadVariant(step.variant);
    GameFile file;
    file.variant = step.variant;
    file.variantId = v.variantId();
    file.startFen = toFen(Position::startPosition(v));
    file.moves = step.moves;
    Game game(v);
    const auto ok = applyGameFile(file, game);
    REQUIRE(ok.has_value());
    CHECK(game.moveHistory().size() == step.moves.size());
  }
}

TEST_CASE("the attract sequence plays every shape and loops back forever",
          "[unit][app][attract]") {
  auto shell = makeShell(tempSettings("attract.conf"));
  shell->settings().watchDwell = 0.05f;
  const AttractSequence seq = defaultAttractSequence();
  shell->startAttract(seq);
  REQUIRE(shell->attracting());
  // Attract is a main-menu mode: it starts a game but must not navigate off the menu.
  REQUIRE(shell->screen() == Screen::MainMenu);
  REQUIRE(shell->session() != nullptr);

  // Walk beat by beat. `advanceAttract` returns true on the tick it cuts to the next
  // shape, so the variants it reports are exactly the sequence's beats, in order.
  std::vector<std::string> seen{shell->currentVariant()};
  constexpr float kDt = 0.02f;
  int ticks = 0;
  for (; ticks < 200000 && seen.size() <= seq.size(); ++ticks) {
    shell->session()->advanceAnimation(kDt);
    if (shell->advanceAttract(kDt)) seen.push_back(shell->currentVariant());
  }
  REQUIRE(seen.size() == seq.size() + 1);  // five beats and the wrap
  for (std::size_t i = 0; i < seq.size(); ++i) {
    CHECK(seen[i] == seq[i].variant);
  }
  // The last cut wrapped to the first beat, and the sequence is still running.
  CHECK(seen.back() == seq.front().variant);
  CHECK(shell->attracting());
  CHECK(shell->attractStep() == 0);
}

TEST_CASE("the attract sequence draws the board behind the main menu only",
          "[unit][app][attract]") {
  auto shell = makeShell(tempSettings("attract-shows.conf"));
  REQUIRE(shell->screen() == Screen::MainMenu);
  CHECK_FALSE(shell->showsBoard());  // nothing running yet: the menu owns the screen
  shell->startAttract(defaultAttractSequence());
  CHECK(shell->showsBoard());  // the board replaces the ambient field (M13.9)
  shell->stopAttract();
  CHECK_FALSE(shell->showsBoard());

  // A running attract never makes any other screen claim the board. Each check navigates
  // straight from the menu, so `previous_` stays the menu and Settings keeps its existing
  // "only over a game, not the menu" rule.
  shell->startAttract(defaultAttractSequence());
  for (const Screen s :
       {Screen::NewGame, Screen::Watch, Screen::Settings, Screen::Welcome}) {
    shell->go(Screen::MainMenu);
    shell->go(s);
    CHECK_FALSE(shell->showsBoard());
  }
}

TEST_CASE("stopping the attract restores the opaque board and leaves the menu",
          "[unit][app][attract]") {
  auto shell = makeShell(tempSettings("attract-stop.conf"));
  shell->settings().watchDwell = 0.05f;
  shell->startAttract(defaultAttractSequence());
  REQUIRE(shell->attracting());
  // Run into the Klein beat (index 3, after the standard opener and two shape beats),
  // which ghosts the board, then stop.
  for (int i = 0; i < 20000 && shell->attractStep() < 3; ++i) {
    shell->session()->advanceAnimation(0.02f);
    shell->advanceAttract(0.02f);
  }
  REQUIRE(shell->attractStep() == 3);
  shell->stopAttract();
  CHECK_FALSE(shell->attracting());
  CHECK_FALSE(shell->watching());
  CHECK(shell->settings().geometryGhost == 1.0f);
}
