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
#include "view/morph.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::unique_ptr<Shell> makeShell(const std::filesystem::path& settings = {}) {
  auto shell = Shell::create(
      {"standard", "mobius", "cylinder", "torus", "klein", "torus3d_8x8", "hyper4"},
      settings);
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
  // Eight beats on six shapes: Mobius and Klein each appear twice - fold in, then ease
  // back out to whatever they folded from or toward, before the next cut (M13.9
  // revision).
  const std::vector<std::string> expected{"standard", "mobius", "mobius", "cylinder",
                                          "torus",    "klein",  "klein",  "torus3d_8x8"};
  REQUIRE(seq.size() == expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CAPTURE(i);
    CHECK(seq[i].variant == expected[i]);
  }
  // The documented M18.6 workaround: open on the real standard board, then cut into the
  // Mobius roll in motion rather than at the rank-mirrored flat end.
  REQUIRE(seq.front().variant == "standard");
  REQUIRE(seq[1].variant == "mobius");
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
  // Attract is a real watch run, so it lands on the ordinary game screen (M13.9 revision,
  // Part G) rather than compositing the board behind the menu.
  REQUIRE(shell->screen() == Screen::Game);
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
  REQUIRE(seen.size() == seq.size() + 1);  // eight beats and the wrap
  for (std::size_t i = 0; i < seq.size(); ++i) {
    CHECK(seen[i] == seq[i].variant);
  }
  // The standard opener plays once; the last cut then loops into the first shape beat
  // (Mobius), and the sequence is still running.
  CHECK(seen.back() == "mobius");
  CHECK(seen.back() == seq[1].variant);
  CHECK(shell->attracting());
  CHECK(shell->attractStep() == 1);
}

TEST_CASE("the attract sequence lands on the game screen and leaves for the menu",
          "[unit][app][attract]") {
  auto shell = makeShell(tempSettings("attract-shows.conf"));
  REQUIRE(shell->screen() == Screen::MainMenu);
  CHECK_FALSE(shell->showsBoard());  // nothing running yet: the menu owns the screen
  // Starting the trailer is an ordinary watch run under the hood, so it lands on the real
  // game screen with the board drawn there - full board, no menu panel beside it (M13.9
  // revision, Part G). `showsBoard()` behaves exactly as it does for a normal game.
  shell->startAttract(defaultAttractSequence());
  CHECK(shell->screen() == Screen::Game);
  CHECK(shell->showsBoard());
  shell->stopAttract();
  CHECK(shell->screen() == Screen::MainMenu);  // the stop control and Esc both land here
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
  // Run into the Klein beat (index 5, after the standard opener, Mobius's two beats,
  // cylinder and torus), which ghosts the board, then stop.
  for (int i = 0; i < 20000 && shell->attractStep() < 5; ++i) {
    shell->session()->advanceAnimation(0.02f);
    shell->advanceAttract(0.02f);
  }
  REQUIRE(shell->attractStep() == 5);
  shell->stopAttract();
  CHECK_FALSE(shell->attracting());
  CHECK_FALSE(shell->watching());
  CHECK(shell->screen() == Screen::MainMenu);
  CHECK(shell->settings().geometryGhost == 1.0f);
  CHECK_FALSE(shell->settings().shapePieceCylinders);    // no icon pucks leak forward
  CHECK(shell->settings().geometryShellSpread == 1.0f);  // nor the collapsed shells
}

TEST_CASE("only the far-side shapes draw flat icon pieces", "[unit][app][attract]") {
  // Part H: the two shapes whose far side a sculptural piece cannot be read through get
  // the flat icon pucks. It is a per-step style, not a continuous pose.
  const AttractSequence seq = defaultAttractSequence();
  for (const AttractStep& step : seq) {
    CAPTURE(step.variant);
    const bool farSide = step.variant == "klein" || step.variant == "torus3d_8x8";
    CHECK(step.flatPieces == farSide);
  }
  // The two that go flat are exactly the two that ghost.
  for (const AttractStep& step : seq) {
    if (step.flatPieces) CHECK(step.ghostTarget < 1.0f);
  }
}

TEST_CASE("the attract cut switches the pieces to the flat icon style",
          "[unit][app][attract]") {
  auto shell = makeShell(tempSettings("attract-flat.conf"));
  shell->settings().watchDwell = 0.05f;
  shell->startAttract(defaultAttractSequence());
  REQUIRE(shell->attracting());
  CHECK_FALSE(shell->settings().shapePieceCylinders);  // standard opens sculptural

  const auto runTo = [&](std::size_t step) {
    for (int i = 0; i < 200000 && shell->attractStep() != step; ++i) {
      shell->session()->advanceAnimation(0.02f);
      shell->advanceAttract(0.02f);
    }
    return shell->attractStep() == step;
  };
  REQUIRE(runTo(3));  // cylinder (after Mobius's two beats): still sculptural
  CHECK(shell->currentVariant() == "cylinder");
  CHECK_FALSE(shell->settings().shapePieceCylinders);
  REQUIRE(runTo(4));  // torus: still sculptural
  CHECK_FALSE(shell->settings().shapePieceCylinders);
  REQUIRE(runTo(5));  // Klein folds in: icon pucks on
  CHECK(shell->currentVariant() == "klein");
  CHECK(shell->settings().shapePieceCylinders);
  REQUIRE(runTo(6));  // Klein eases back out: still icon pucks, still Klein
  CHECK(shell->currentVariant() == "klein");
  CHECK(shell->settings().shapePieceCylinders);
  REQUIRE(runTo(7));  // 3-D torus: icon pucks on
  CHECK(shell->currentVariant() == "torus3d_8x8");
  CHECK(shell->settings().shapePieceCylinders);
  REQUIRE(runTo(1));  // back round to Mobius: sculptural again
  CHECK(shell->currentVariant() == "mobius");
  CHECK_FALSE(shell->settings().shapePieceCylinders);
}

TEST_CASE("every cut in the sequence lands on the previous shape's own end-of-stage",
          "[unit][app][attract]") {
  // M13.9 revision: the whole point is that a cut is a continuous morph, not a hard swap
  // - which requires each beat to *start* exactly where the one before it *ends*. This
  // pins the wiring that makes that true, step by step through the sequence.
  const AttractSequence seq = defaultAttractSequence();
  REQUIRE(seq.size() == 8);
  const AttractStep& standard = seq[0];
  const AttractStep& mobiusIn = seq[1];
  const AttractStep& mobiusOut = seq[2];
  const AttractStep& cylinder = seq[3];
  const AttractStep& torus = seq[4];
  const AttractStep& kleinIn = seq[5];
  const AttractStep& kleinOut = seq[6];
  const AttractStep& shells = seq[7];

  // Mobius's second beat eases all the way down to the bare flat board - the one point
  // its own construction and cylinder's share (proven directly in
  // test_overture_scene.cpp, not just argued) - rather than a mid-morph window.
  CHECK(mobiusOut.variant == "mobius");
  CHECK(mobiusOut.moves.empty());
  CHECK(mobiusOut.formedTarget == 0.0f);

  // Cylinder inherits that same flat waypoint (no `formedFrom` of its own - the default
  // "wherever the previous beat left it" is exactly right here) and ends fully formed
  // (an open tube); torus starts at its own open-tube waypoint - the same picture,
  // bit-for-bit, by construction of the two morphs.
  CHECK(cylinder.formedFrom < 0.0f);
  CHECK(cylinder.formedTarget == 1.0f);
  CHECK(torus.formedFrom == view::kTorusRollEnd);
  CHECK(torus.formedTarget == 1.0f);

  // Torus ends fully formed (a plain ring); Klein starts at its own ring-formed waypoint
  // - no fold, no twist yet - the same picture torus's own end state is.
  CHECK(kleinIn.formedFrom == view::kKleinRingEnd);
  CHECK(kleinIn.formedTarget == 1.0f);

  // Klein's second beat eases back down to that same ring-formed waypoint - undoing the
  // fold and the twist it just played - rather than starting fresh or snapping.
  CHECK(kleinOut.variant == "klein");
  CHECK(kleinOut.moves.empty());
  CHECK(kleinOut.formedTarget == view::kKleinRingEnd);

  // The 3-D torus is explicitly pinned fully formed (not inherited from Klein's own
  // `kKleinRingEnd`, which does not mean the same thing on a stacked shape) and starts
  // with every level collapsed onto one ring, spreading apart over the beat.
  CHECK(shells.formedFrom == 1.0f);
  CHECK(shells.spreadFrom == 0.0f);
  CHECK(shells.spreadTarget == 1.0f);

  // Standard has nothing to morph from (the sequence's own cold open); Mobius's first
  // beat is the M18.6 reveal workaround, same shape as cylinder's used to be.
  CHECK(standard.variant == "standard");
  CHECK(mobiusIn.formedFrom > 0.0f);
}
