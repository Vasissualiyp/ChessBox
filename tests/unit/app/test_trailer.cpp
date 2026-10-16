// SPDX-License-Identifier: GPL-3.0-or-later
//
// M13.9 revision: the trailer owns its camera. A calm default that never follows a piece,
// an edit mode that freezes the trailer so a camera can be authored, and stepping through
// the chain. Headless, driving a Shell and a clock.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

#include "app/shell.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::filesystem::path tempSettings(const char* name) {
  const auto dir = std::filesystem::temp_directory_path() / "chessbox-tests";
  std::filesystem::create_directories(dir);
  const auto path = dir / name;
  std::filesystem::remove(path);
  std::filesystem::remove(dir / "camera_track.txt");
  return path;
}

std::unique_ptr<Shell> makeShell(const char* name) {
  auto shell = Shell::create({"standard", "cylinder", "torus", "klein", "torus3d_8x8"},
                             tempSettings(name));
  shell->setVariantLoader([](const std::string& n) -> Result<VariantSpec> {
    auto v = loadVariantFile(test::variantPath(n));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  shell->dismissWelcome();
  return shell;
}

void run(Shell& shell, float seconds) {
  constexpr float kStep = 1.0f / 60.0f;
  for (float t = 0.0f; t < seconds; t += kStep) {
    if (shell.session() != nullptr) shell.session()->advanceAnimation(kStep);
    shell.advanceAttract(kStep);
    if (shell.hasGame()) shell.advanceCameraTrack(kStep);
  }
}

}  // namespace

TEST_CASE("the trailer never follows a piece, even on a flat board with route set",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-follow.conf");
  shell->settings().cameraMode = "route";
  shell->settings().followStrength = 1.0f;
  shell->startAttract(defaultAttractSequence());
  REQUIRE(shell->attracting());
  REQUIRE(shell->currentVariant() == "standard");
  CHECK_FALSE(shell->session()->followsMove());
  // The player's own preference is theirs: the trailer must not rewrite it.
  CHECK(shell->settings().cameraMode == "route");
  // ... and the same on the next beat's fresh session.
  run(*shell, 400.0f);
  CHECK_FALSE(shell->session()->followsMove());
}

TEST_CASE("the Follow camera mode restores the chase", "[unit][app][trailer]") {
  auto shell = makeShell("trailer-follow2.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Follow);
  CHECK(shell->session()->followsMove());
  shell->setTrailerCamera(TrailerCamera::Auto);
  CHECK_FALSE(shell->session()->followsMove());
}

TEST_CASE("the Auto camera sways a few degrees and Authored holds still",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-sway.conf");
  shell->startAttract(defaultAttractSequence());
  run(*shell, 10.0f);
  const view::OrbitCamera base = shell->session()->camera();
  const view::OrbitCamera swayed = shell->trailerView(base);
  const float d = std::abs(swayed.yaw - base.yaw);
  CHECK(d > 0.0f);
  CHECK(d < 0.1f);
  CHECK(swayed.target.x == base.target.x);

  shell->setTrailerCamera(TrailerCamera::Authored);
  run(*shell, 5.0f);  // the sway fades out
  const view::OrbitCamera held = shell->session()->camera();
  CHECK(shell->trailerView(held).yaw == held.yaw);
  CHECK(shell->trailerView(held).pitch == held.pitch);
}

TEST_CASE("the trailer's own camera does nothing outside the trailer",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-outside.conf");
  REQUIRE(shell->startGame("standard").has_value());
  const view::OrbitCamera c = shell->session()->camera();
  CHECK(shell->trailerView(c).yaw == c.yaw);
}

TEST_CASE("edit mode freezes the trailer and selects the authored camera",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-edit.conf");
  shell->startAttract(defaultAttractSequence());
  run(*shell, 3.0f);
  shell->setTrailerEditing(true);
  CHECK(shell->trailerEditing());
  CHECK(shell->trailerCamera() == TrailerCamera::Authored);
  const std::size_t beat = shell->attractStep();
  const std::size_t index = shell->session()->watchIndex();
  run(*shell, 300.0f);  // far longer than the whole trailer
  CHECK(shell->attractStep() == beat);
  CHECK(shell->session()->watchIndex() == index);
  shell->setTrailerEditing(false);
  run(*shell, 5.0f);
  // Resumed: the opener is brisk enough to be on a later move, or already cut onward.
  CHECK((shell->attractStep() > beat || shell->session()->watchIndex() > index));
}

TEST_CASE("stepping in edit mode lands on the requested ply", "[unit][app][trailer]") {
  auto shell = makeShell("trailer-step.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  CHECK_FALSE(shell->trailerStep(-5));  // cannot go before the start
  for (int i = 0; i < 6; ++i) CHECK(shell->trailerStep(+1));
  run(*shell, 3.0f);
  CHECK(shell->session()->game().moveHistory().size() == 7);
  CHECK(shell->trailerStep(-1));
  CHECK(shell->session()->game().moveHistory().size() == 6);
  CHECK(shell->trailerStep(-6));
  CHECK(shell->session()->game().moveHistory().size() == 0);
  // Not in edit mode: stepping is refused rather than racing the trailer clock.
  shell->setTrailerEditing(false);
  CHECK_FALSE(shell->trailerStep(+1));
}

TEST_CASE("jumping to a beat replays the chain so it continues the position",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-goto.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  shell->trailerGoto(2, 0);
  CHECK(shell->attractStep() == 2);
  CHECK(shell->currentVariant() == "torus");
  // A torus beat that continues the game is never the 32-piece opening position.
  const auto& pos = shell->session()->game().position();
  int pieces = 0;
  for (CellId c = 0; c < pos.cellCount(); ++c) {
    if (!pos.isEmpty(c)) ++pieces;
  }
  CHECK(pieces < 32);
  CHECK(pieces > 4);
  // And a jump backwards still works.
  shell->trailerGoto(0, 0);
  CHECK(shell->attractStep() == 0);
  CHECK(shell->currentVariant() == "standard");
  // The next advance reports the cut so the front end re-frames.
  CHECK(shell->advanceAttract(1.0f / 60.0f));
}

TEST_CASE(
    "recording during the trailer pins the keyframe to its beat and plays only in "
    "the authored mode",
    "[unit][app][trailer]") {
  auto shell = makeShell("trailer-record.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  shell->setTrailerCamera(TrailerCamera::Auto);  // recording must switch to Authored
  shell->trailerGoto(1, 0);
  Action orbit;
  orbit.kind = ActionKind::Orbit;
  orbit.dx = 0.5f;
  (void)shell->session()->apply(orbit);
  shell->settings().geometryInvert = true;
  shell->recordCameraKeyframe(8.0f);
  CHECK(shell->trailerCamera() == TrailerCamera::Authored);
  const std::size_t ply = shell->session()->game().moveHistory().size();
  const CameraKeyframe* kf = shell->cameraTrack().find({"cylinder", ply, 2});
  REQUIRE(kf != nullptr);
  CHECK(kf->invert);
  // The ordinary-game cue is distinct.
  CHECK(shell->cameraTrack().find({"cylinder", ply, 0}) == nullptr);
}

TEST_CASE("changing a setting mid-trailer does not hand the camera back to the player",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-apply.conf");
  shell->settings().cameraMode = "route";
  shell->settings().followStrength = 1.0f;
  shell->startAttract(defaultAttractSequence());
  shell->applySettings();
  CHECK_FALSE(shell->session()->followsMove());
}

TEST_CASE("the standard opener is short and the whole chain is one continuous game",
          "[unit][app][trailer]") {
  const AttractSequence seq = defaultAttractSequence();
  REQUIRE_FALSE(seq.empty());
  CHECK(seq.front().variant == "standard");
  // "By move 20 people will click off": the flat board gets at most 20 plies.
  CHECK(seq.front().moves.size() <= 20);
  CHECK(seq.front().dwell >= 0.0f);  // and plays briskly
  CHECK(seq.front().dwell < 0.4f);
  for (std::size_t i = 1; i < seq.size(); ++i) {
    CAPTURE(i);
    CHECK(seq[i].continuesPosition);
    CHECK(seq[i].moves.size() <= 10);
  }
}

TEST_CASE("the layer colour swap is a persisted setting, off by default",
          "[unit][app][trailer]") {
  const auto path = tempSettings("layer-swap.conf");
  Settings s;
  CHECK_FALSE(s.swapLayerColors);
  s.swapLayerColors = true;
  REQUIRE(s.save(path).has_value());
  CHECK(Settings::load(path).swapLayerColors);
}

TEST_CASE("trailer look overrides survive the beat cut and clear on stop",
          "[unit][app][trailer]") {
  auto shell = makeShell("trailer-override.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerFlatPieces(true);
  shell->setTrailerGhost(0.5f);
  run(*shell, 400.0f);  // across several cuts
  CHECK(shell->settings().shapePieceCylinders);
  CHECK(shell->settings().geometryGhost < 0.99f);
  shell->stopAttract();
  CHECK_FALSE(shell->settings().shapePieceCylinders);
  CHECK(shell->settings().geometryGhost == 1.0f);
}

TEST_CASE("the trailer previews each move for the configured time",
          "[unit][app][trailer][preview]") {
  auto shell = makeShell("trailer-preview.conf");
  CHECK(shell->settings().trailerMovePreview == 0.3f);
  shell->settings().trailerMovePreview = 9.0f;
  shell->settings().sanitize();
  CHECK(shell->settings().trailerMovePreview == 0.5f);
  shell->settings().trailerMovePreview = 0.3f;
  shell->startAttract(defaultAttractSequence());
  bool sawPreview = false;
  constexpr float kStep = 1.0f / 60.0f;
  for (int i = 0; i < 600 && !sawPreview; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
    sawPreview = shell->session()->selected() != kInvalidCell;
  }
  CHECK(sawPreview);
}

TEST_CASE("a beat jump asked for by the interface waits for the next frame",
          "[unit][app][trailer]") {
  // The interface holds a reference to the session for the rest of its frame; a jump
  // replaces the session, so doing it mid-frame left the panel on a dead one and crashed.
  auto shell = makeShell("trailer-defer.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  const Session* before = shell->session();
  shell->requestTrailerJumpBeat(+1);
  CHECK(shell->session() == before);  // nothing happened yet
  CHECK(shell->attractStep() == 0);
  CHECK(shell->advanceAttract(1.0f / 60.0f));  // applied here, reported as a cut
  CHECK(shell->attractStep() == 1);
  shell->requestTrailerJumpBeat(-1);
  CHECK(shell->advanceAttract(1.0f / 60.0f));
  CHECK(shell->attractStep() == 0);
  // Jumping backwards from the first beat wraps to the last.
  shell->requestTrailerJumpBeat(-1);
  CHECK(shell->advanceAttract(1.0f / 60.0f));
  CHECK(shell->attractStep() == shell->attractCount() - 1);
  // A request outside edit mode, or after the trailer stops, is dropped.
  shell->setTrailerEditing(false);
  shell->requestTrailerJumpBeat(+1);
  shell->stopAttract();
  CHECK_FALSE(shell->advanceAttract(1.0f / 60.0f));
}

namespace {

/// A keyframe on the trailer's first beat, `ply` moves in, with a given yaw and morph.
CameraKeyframe trailerKey(std::size_t beat, std::size_t ply, std::size_t sub, float yaw,
                          float formed = 1.0f, const char* variant = "standard") {
  CameraKeyframe kf;
  kf.cue = {variant, ply, beat + 1, sub};
  kf.camera.yaw = yaw;
  kf.camera.pitch = 0.8f;
  kf.camera.distance = 12.0f;
  kf.formed = formed;
  return kf;
}

}  // namespace

TEST_CASE("between keyframes the trailer camera keeps moving smoothly",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-spline.conf");
  shell->settings().watchDwell = 0.3f;
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Authored);
  for (const auto& [ply, yaw] : {std::pair{1, 0.0f}, std::pair{5, 0.6f},
                                 std::pair{9, 0.2f}, std::pair{13, 1.0f}}) {
    shell->cameraTrack().set(trailerKey(0, static_cast<std::size_t>(ply), 0, yaw));
  }
  constexpr float kStep = 1.0f / 60.0f;
  float prev = shell->session()->camera().yaw;
  int still = 0, frames = 0;
  float worst = 0.0f;
  for (int i = 0; i < 60 * 40 && shell->attractStep() == 0; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
    shell->advanceCameraTrack(kStep);
    const std::size_t ply = shell->session()->game().moveHistory().size();
    const float yaw = shell->session()->camera().yaw;
    if (ply >= 2 && ply <= 12) {  // strictly between the first and last keyframe
      ++frames;
      if (std::abs(yaw - prev) < 1e-7f) ++still;
      worst = std::max(worst, std::abs(yaw - prev));
    }
    prev = yaw;
  }
  REQUIRE(frames > 100);
  // A chain of eased legs stops dead at every keyframe; a spline never does.
  CHECK(still < frames / 20);
  CHECK(worst < 0.02f);  // no jump from frame to frame
}

TEST_CASE("the 3-D morph is part of a trailer keyframe and a beat still ends",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-morph.conf");
  shell->settings().watchDwell = 0.05f;
  shell->settings().trailerMovePreview = 0.0f;
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerCamera(TrailerCamera::Authored);
  shell->cameraTrack().set(trailerKey(1, 1, 0, 0.3f, 0.2f, "cylinder"));
  shell->cameraTrack().set(trailerKey(1, 9, 0, 0.3f, 0.8f, "cylinder"));
  bool sawLow = false, sawHigh = false;
  constexpr float kStep = 1.0f / 60.0f;
  for (int i = 0; i < 60 * 120 && shell->attractStep() < 3; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
    shell->advanceCameraTrack(kStep);
    if (shell->attractStep() == 1) {
      const std::size_t ply = shell->session()->game().moveHistory().size();
      if (ply == 1)
        sawLow = sawLow || std::abs(shell->settings().geometryFormed - 0.2f) < 0.05f;
      if (ply == 9)
        sawHigh = sawHigh || std::abs(shell->settings().geometryFormed - 0.8f) < 0.05f;
    }
  }
  CHECK(sawLow);
  CHECK(sawHigh);
  CHECK(shell->attractStep() >=
        3);  // the authored morph did not stop the beats advancing
}

TEST_CASE("fractional steps move through a move on the chosen grid",
          "[unit][app][trailer][spline]") {
  auto shell = makeShell("trailer-grid.conf");
  shell->startAttract(defaultAttractSequence());
  shell->setTrailerEditing(true);
  CHECK(shell->trailerGrid() == 1);
  shell->setTrailerGrid(4);
  CHECK(shell->trailerGrid() == 4);
  shell->setTrailerGrid(3);  // not a grid: refused
  CHECK(shell->trailerGrid() == 4);
  const std::size_t ply = shell->session()->game().moveHistory().size();
  CHECK(shell->trailerEditSub() == 0);
  CHECK(shell->trailerStepFraction(+1));
  CHECK(shell->trailerEditSub() == 2);  // a quarter, in eighths
  CHECK(shell->trailerStepFraction(+1));
  CHECK(shell->trailerEditSub() == 4);
  CHECK(shell->session()->game().moveHistory().size() == ply);
  for (int i = 0; i < 2; ++i) CHECK(shell->trailerStepFraction(+1));
  // Four quarters later it is the next move, back at its start.
  CHECK(shell->trailerEditSub() == 0);
  CHECK(shell->session()->game().moveHistory().size() == ply + 1);
  CHECK(shell->trailerStepFraction(-1));
  CHECK(shell->trailerEditSub() == 6);
  CHECK(shell->session()->game().moveHistory().size() == ply);
  // A keyframe recorded here is on the fractional cue.
  shell->setTrailerGrid(8);
  CHECK(shell->trailerStepFraction(-1));
  shell->recordCameraKeyframe(2.0f);
  CHECK(shell->cameraTrack().find({"standard", ply, 1, 5}) != nullptr);
}

TEST_CASE("restarting the trailer goes back to the first beat", "[unit][app][trailer]") {
  auto shell = makeShell("trailer-restart.conf");
  shell->settings().watchDwell = 0.05f;
  shell->startAttract(defaultAttractSequence());
  constexpr float kStep = 1.0f / 60.0f;
  for (int i = 0; i < 60 * 60 && shell->attractStep() < 2; ++i) {
    shell->session()->advanceAnimation(kStep);
    shell->advanceAttract(kStep);
  }
  REQUIRE(shell->attractStep() >= 2);
  shell->setTrailerCamera(TrailerCamera::Authored);
  const Session* before = shell->session();
  shell->requestTrailerRestart();
  CHECK(shell->session() == before);    // deferred: the interface holds the session
  CHECK(shell->advanceAttract(kStep));  // reported as a cut so the front end re-frames
  CHECK(shell->attractStep() == 0);
  CHECK(shell->currentVariant() == "standard");
  CHECK(shell->attracting());
  CHECK(shell->trailerCamera() == TrailerCamera::Authored);  // the mode is kept
  CHECK(shell->session()->game().moveHistory().size() == 1);
}
