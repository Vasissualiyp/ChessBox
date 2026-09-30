// SPDX-License-Identifier: GPL-3.0-or-later
//
// The library screen's overtures: which one a variant asks for, and the rule that an
// overture is never cut off mid-shape. All of it with no window and no clock, which is
// the reason the state machine lives in the app layer rather than in the interface.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "app/overture.hpp"

using namespace cb;
using namespace cb::app;
using Catch::Matchers::WithinAbs;

namespace {

/// Run the player forward far enough to reach the formed shape and start back.
void runTo(OverturePlayer& p, OverturePlayer::Phase want, float step = 0.05f) {
  for (int i = 0; i < 4000 && p.phase() != want; ++i) p.advance(step);
  REQUIRE(p.phase() == want);
}

/// Advance until the player is actually showing `want`. With a selection no longer
/// interrupting the shape on screen, a swap is several phases away.
void runUntilShowing(OverturePlayer& p, Overture want, float step = 0.05f) {
  for (int i = 0; i < 4000 && p.current() != want; ++i) p.advance(step);
  REQUIRE(p.current() == want);
}

}  // namespace

TEST_CASE("every shipped variant asks for an overture", "[unit]") {
  // The twelve with a bespoke one.
  CHECK(overtureFor("standard") == Overture::Standard);
  CHECK(overtureFor("cylinder") == Overture::Cylinder);
  CHECK(overtureFor("torus") == Overture::Torus);
  CHECK(overtureFor("mobius") == Overture::Mobius);
  CHECK(overtureFor("klein") == Overture::Klein);
  CHECK(overtureFor("mirrorbox") == Overture::Mirrorbox);
  CHECK(overtureFor("cube5") == Overture::Cube5);
  CHECK(overtureFor("hyper4") == Overture::Hyper4);
  CHECK(overtureFor("atomic") == Overture::Atomic);
  CHECK(overtureFor("atomic_torus") == Overture::AtomicTorus);
  CHECK(overtureFor("mustcapture") == Overture::MustCapture);
  CHECK(overtureFor("5d") == Overture::Multiverse);

  // `atomic_torus` must not be matched by `atomic`'s prefix: it is the combination
  // that the variant exists to show, and showing the flat explosion for it would
  // throw away the whole point.
  CHECK(overtureFor("atomic_torus") != overtureFor("atomic"));

  // The two higher tori no longer borrow the plain torus: the whole point of the T6
  // overture is that six axes have nowhere to go, which the torus overture cannot say.
  CHECK(overtureFor("torus3d") == Overture::Torus3d);
  CHECK(overtureFor("t6") == Overture::T6);

  // The one with none of its own borrows the nearest family rather than leaving the pane
  // blank. A library entry always animates.
  CHECK(overtureFor("charged") == Overture::Standard);

  // And so does a name nobody has seen - a Workshop package, eventually.
  CHECK(overtureFor("something-nobody-shipped") == Overture::Standard);
  CHECK(overtureFor("") == Overture::Standard);
}

TEST_CASE("an overture cycles out and back", "[unit]") {
  OverturePlayer p;
  CHECK(p.current() == Overture::Standard);
  CHECK_THAT(p.progress(), WithinAbs(0.0f, 1e-6f));

  p.advance(OverturePlayer::kSweep * 0.5f);
  CHECK(p.phase() == OverturePlayer::Phase::Forward);
  CHECK_THAT(p.progress(), WithinAbs(0.5f, 1e-4f));

  runTo(p, OverturePlayer::Phase::Held);
  CHECK_THAT(p.progress(), WithinAbs(1.0f, 1e-6f));

  // It dwells on the formed shape rather than turning round the instant it arrives.
  p.advance(OverturePlayer::kHoldFormed * 0.5f);
  CHECK(p.phase() == OverturePlayer::Phase::Held);
  CHECK_THAT(p.progress(), WithinAbs(1.0f, 1e-6f));

  runTo(p, OverturePlayer::Phase::Reverse);
  runTo(p, OverturePlayer::Phase::Settled);
  CHECK_THAT(p.progress(), WithinAbs(0.0f, 1e-6f));

  // With nothing queued it simply goes again.
  runTo(p, OverturePlayer::Phase::Forward);
  CHECK(p.current() == Overture::Standard);
}

TEST_CASE("progress never leaves zero to one", "[unit]") {
  OverturePlayer p;
  // A frame far longer than a whole sweep - a stalled machine, or a debugger step -
  // must not throw t past the end, because every scene is a function of t on [0,1].
  for (int i = 0; i < 40; ++i) {
    p.advance(9.0f);
    CHECK(p.progress() >= 0.0f);
    CHECK(p.progress() <= 1.0f);
  }
}

TEST_CASE("choosing another variant unwinds the shape first", "[unit]") {
  OverturePlayer p;
  runTo(p, OverturePlayer::Phase::Held);
  REQUIRE(p.current() == Overture::Standard);

  p.select("torus");
  // The formed board does not vanish: it is still the standard overture on screen, with
  // the torus stored behind it, and it unwinds when its own dwell is up.
  CHECK(p.current() == Overture::Standard);
  CHECK(p.pending() == Overture::Torus);
  CHECK(p.phase() == OverturePlayer::Phase::Held);
  runTo(p, OverturePlayer::Phase::Reverse);

  runTo(p, OverturePlayer::Phase::Settled);
  CHECK(p.current() == Overture::Standard);
  CHECK_THAT(p.progress(), WithinAbs(0.0f, 1e-6f));

  // Only once it is flat does the swap happen.
  runUntilShowing(p, Overture::Torus);
  CHECK(p.pending() == Overture::None);
}

TEST_CASE("a selection mid-forward does not interrupt the shape", "[unit]") {
  OverturePlayer p;
  p.advance(OverturePlayer::kSweep * 0.4f);
  REQUIRE(p.phase() == OverturePlayer::Phase::Forward);
  const float wasAt = p.progress();

  p.select("klein");
  // Still forming, still at the same point, with the klein stored behind it. The shape
  // on screen owns its own clock: it finishes, dwells, and unwinds before anything else
  // is allowed to start.
  CHECK(p.phase() == OverturePlayer::Phase::Forward);
  CHECK(p.current() == Overture::Standard);
  CHECK(p.pending() == Overture::Klein);
  CHECK_THAT(p.progress(), WithinAbs(wasAt, 1e-6f));

  // It does reach the formed shape, rather than turning round early.
  runTo(p, OverturePlayer::Phase::Held);
  CHECK_THAT(p.progress(), WithinAbs(1.0f, 1e-6f));
  CHECK(p.current() == Overture::Standard);
  runUntilShowing(p, Overture::Klein);
}

TEST_CASE("only the latest selection is queued", "[unit]") {
  OverturePlayer p;
  runTo(p, OverturePlayer::Phase::Held);

  // Running down the library with the arrow keys must not enqueue every entry it
  // passes: the player is only still interested in the last one.
  p.select("torus");
  p.select("klein");
  p.select("cube5");
  CHECK(p.pending() == Overture::Cube5);

  runUntilShowing(p, Overture::Cube5);
  CHECK(p.pending() == Overture::None);
}

TEST_CASE("picking the running variant again cancels a queued change", "[unit]") {
  OverturePlayer p;
  runTo(p, OverturePlayer::Phase::Held);
  p.select("torus");
  REQUIRE(p.pending() == Overture::Torus);

  p.select("standard");  // back to the one already on screen
  CHECK(p.pending() == Overture::None);
  runTo(p, OverturePlayer::Phase::Forward);
  CHECK(p.current() == Overture::Standard);
}

TEST_CASE("a selection while flat starts at once", "[unit]") {
  OverturePlayer p;
  runTo(p, OverturePlayer::Phase::Held);
  runTo(p, OverturePlayer::Phase::Settled);
  REQUIRE(p.phase() == OverturePlayer::Phase::Settled);

  p.select("atomic");
  CHECK(p.current() == Overture::Atomic);
  CHECK(p.pending() == Overture::None);
  CHECK(p.phase() == OverturePlayer::Phase::Forward);
  CHECK_THAT(p.progress(), WithinAbs(0.0f, 1e-6f));
}

TEST_CASE("the board is built once and never again", "[unit]") {
  OverturePlayer p;
  // First entry to the screen: standard is selected and the construction is pending.
  REQUIRE(p.current() == Overture::Standard);
  CHECK(p.intro());

  // It is spent at the formed shape, where both versions of that scene agree, so
  // nothing visible changes at the moment the flag drops.
  runTo(p, OverturePlayer::Phase::Held);
  CHECK_FALSE(p.intro());

  // And it stays spent through a full cycle and a retrigger.
  runTo(p, OverturePlayer::Phase::Settled);
  runTo(p, OverturePlayer::Phase::Forward);
  CHECK_FALSE(p.intro());

  p.select("torus");
  runUntilShowing(p, Overture::Torus);
  p.select("standard");
  runUntilShowing(p, Overture::Standard);
  CHECK_FALSE(p.intro());
}

TEST_CASE("only the standard overture has a construction", "[unit]") {
  OverturePlayer p;
  p.select(Overture::Torus);
  runUntilShowing(p, Overture::Torus);
  // `intro` is a question about the scene being drawn, not a flag the player holds
  // in the abstract: no other overture builds a board, so none of them may claim it.
  CHECK_FALSE(p.intro());
}

TEST_CASE("a capture can switch outright", "[unit]") {
  // The unwind rule is about a player moving through the library. A screenshot is not,
  // so `--shot` gets a door out of it.
  OverturePlayer p;
  runTo(p, OverturePlayer::Phase::Held);
  p.jumpTo(Overture::Klein);
  CHECK(p.current() == Overture::Klein);
  CHECK(p.pending() == Overture::None);
  CHECK(p.phase() == OverturePlayer::Phase::Forward);
  CHECK(p.progress() == 0.0f);
}

TEST_CASE("a capture can set the cycle to an exact point", "[unit]") {
  // `captureFrame` feeds a fixed timestep on purpose, so a reproducible screenshot
  // has to be able to state its t rather than integrate towards one.
  OverturePlayer p;
  p.setProgress(0.62f);
  CHECK_THAT(p.progress(), WithinAbs(0.62f, 1e-6f));
  p.setProgress(4.0f);
  CHECK_THAT(p.progress(), WithinAbs(1.0f, 1e-6f));
  p.setProgress(-1.0f);
  CHECK_THAT(p.progress(), WithinAbs(0.0f, 1e-6f));
}
