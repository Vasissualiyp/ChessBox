// SPDX-License-Identifier: GPL-3.0-or-later
//
// M12.8 "Watch a game": the interactive read-only session. A bundled game is played by
// the engine through the ordinary session, one move at a time, while the player watches.
// The mode is driven by the real interactive clock, not a fixed `t`, so there is no
// determinism contract here - the capture path (`--play --clip`) keeps that. These tests
// are headless: they drive a Session and some floats, never a window.
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

#include "app/session.hpp"
#include "io/fen.hpp"
#include "io/game_file.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

constexpr const char* kStart = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

GameFile standardGame(std::vector<std::string> moves) {
  GameFile file;
  file.variant = "standard";
  file.variantId = 0;
  file.startFen = kStart;
  file.moves = std::move(moves);
  return file;
}

std::unique_ptr<Session> standardSession() {
  auto session = Session::create(test::loadVariant("standard"));
  if (!session.has_value()) throw std::runtime_error(session.error().format());
  return std::move(*session);
}

CellId cell(const Session& s, const char* name) {
  auto c = parseCell(s.variant().dims, name);
  if (!c.has_value()) throw std::runtime_error("no such cell");
  return *c;
}

/// The interactive loop in miniature: advance the move body and the watch clock together
/// until the watch ends. A tiny dwell keeps the test fast.
void runWatch(Session& s) {
  for (int i = 0; i < 20000 && s.watching(); ++i) {
    s.advanceAnimation(0.05f);
    s.advanceWatching(0.05f);
  }
}

}  // namespace

TEST_CASE("starting to watch plays the first move and locks the board",
          "[unit][app][watch]") {
  auto s = standardSession();
  REQUIRE(s->startWatching(standardGame({"e2e4", "e7e5", "g1f3"}), 0.1f).has_value());
  REQUIRE(s->watching());
  // The first move is already in flight, so the board shows something happening.
  REQUIRE(s->game().moveHistory().size() == 1);

  // The board is interaction-locked while the engine plays, the same gate a shot in
  // flight uses - not a second lock.
  REQUIRE(s->boardLocked());

  // A click on one of the mover's own pieces does nothing: no selection, no move.
  Action click;
  click.kind = ActionKind::ClickCell;
  click.cell = cell(*s, "e2");
  REQUIRE(s->apply(click).has_value());
  CHECK(s->selected() == kInvalidCell);
  CHECK(s->game().moveHistory().size() == 1);
}

TEST_CASE("a board click at a real cell is refused while watching",
          "[unit][app][watch]") {
  auto s = standardSession();
  REQUIRE(s->startWatching(standardGame({"e2e4", "e7e5"}), 0.1f).has_value());
  REQUIRE(s->watching());

  // The pixel path the mouse takes must resolve to nothing, exactly as it does during a
  // followed shot: the board is not the player's while a game is being played.
  REQUIRE(s->clickPixel(400.0f, 300.0f, 800.0f, 600.0f) == kInvalidCell);
  CHECK(s->selected() == kInvalidCell);
}

TEST_CASE("the watch plays every move and ends when the file runs out",
          "[unit][app][watch]") {
  const GameFile file = standardGame({"e2e4", "e7e5", "g1f3"});
  auto s = standardSession();
  REQUIRE(s->startWatching(file, 0.1f).has_value());
  runWatch(*s);
  REQUIRE_FALSE(s->watching());
  CHECK(s->game().moveHistory().size() == 3);

  // The position matches the same file played in bulk.
  const VariantSpec spec = test::loadVariant("standard");
  Game reference(spec);
  REQUIRE(applyGameFile(file, reference).has_value());
  CHECK(toFen(s->game().position()) == toFen(reference.position()));
}

TEST_CASE("an illegal move stops the watch and keeps the legal prefix",
          "[unit][app][watch]") {
  auto s = standardSession();
  // The second move is not legal in the position after e2e4.
  REQUIRE(s->startWatching(standardGame({"e2e4", "e2e4"}), 0.1f).has_value());
  runWatch(*s);
  REQUIRE_FALSE(s->watching());
  CHECK(s->game().moveHistory().size() == 1);
  // The loader's own wording, naming the move and the position.
  CHECK(s->message().find("move 2 ('e2e4')") != std::string::npos);
}

TEST_CASE("stopWatching leaves the ordinary session and allows input again",
          "[unit][app][watch]") {
  auto s = standardSession();
  REQUIRE(s->startWatching(standardGame({"e2e4", "e7e5"}), 0.1f).has_value());
  s->stopWatching();
  REQUIRE_FALSE(s->watching());
  REQUIRE_FALSE(s->boardLocked());
  // A real click now selects a piece: the lock has been released, not just hidden.
  // Only e2e4 has played, so it is Black to move and a black piece is selectable.
  Action click;
  click.kind = ActionKind::ClickCell;
  click.cell = cell(*s, "e7");
  REQUIRE(s->apply(click).has_value());
  CHECK(s->selected() == click.cell);
}
