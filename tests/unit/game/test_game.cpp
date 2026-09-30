// SPDX-License-Identifier: GPL-3.0-or-later
//
// A game can end by concession or agreement, not only by the board.
#include <catch2/catch_test_macros.hpp>

#include "game/game.hpp"
#include "support/variants.hpp"

using namespace cb;

TEST_CASE("resigning and an agreed draw end the game", "[unit][game]") {
  const VariantSpec v = test::loadVariant("standard");

  SECTION("the side that resigns loses") {
    Game g(v);
    REQUIRE(g.result() == GameResult::InProgress);
    g.resign(Color::White);
    CHECK(g.result() == GameResult::BlackWins);
    CHECK(g.endReason() == EndReason::Resignation);
    // A finished game is not changed by another concession.
    g.resign(Color::Black);
    CHECK(g.result() == GameResult::BlackWins);
  }
  SECTION("an agreed draw is a draw") {
    Game g(v);
    g.agreeDraw();
    CHECK(g.result() == GameResult::Draw);
    CHECK(g.endReason() == EndReason::Agreement);
  }
  SECTION("reset clears it") {
    Game g(v);
    g.agreeDraw();
    g.reset();
    CHECK(g.result() == GameResult::InProgress);
  }
}
