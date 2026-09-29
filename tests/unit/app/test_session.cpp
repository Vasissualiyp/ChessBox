// SPDX-License-Identifier: GPL-3.0-or-later
// The whole interaction path, driven by scripts, with no window and no screenshots.
//
// Selection, illegal clicks, promotion choice, undo and view changes are all just
// actions in, snapshot out - so the flows a player actually performs are covered by
// ordinary assertions rather than by someone clicking around (M4.6).
#include "app/session.hpp"

#include <catch2/catch_test_macros.hpp>

#include "io/notation.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::unique_ptr<Session> open(const char* variantName) {
  auto s = Session::create(test::loadVariant(variantName));
  REQUIRE(s.has_value());
  return std::move(*s);
}

CellId cell(const Session& s, const char* name) {
  auto c = parseCell(s.variant().dims, name);
  REQUIRE(c.has_value());
  return *c;
}

}  // namespace

TEST_CASE("clicking a piece selects it and shows its legal moves", "[unit][app]") {
  auto s = open("standard");
  REQUIRE(s->selected() == kInvalidCell);
  REQUIRE(s->snapshot().highlighted().empty());

  REQUIRE(s->applyScript("click e2").has_value());
  REQUIRE(s->selected() == cell(*s, "e2"));
  // A pawn on its home rank has exactly two destinations, and they are the engine's,
  // not the renderer's idea of them.
  REQUIRE(s->snapshot().highlighted().size() == 2);
  const auto lit = s->snapshot().highlighted();
  REQUIRE(std::find(lit.begin(), lit.end(), cell(*s, "e3")) != lit.end());
  REQUIRE(std::find(lit.begin(), lit.end(), cell(*s, "e4")) != lit.end());
}

TEST_CASE("clicking a highlighted destination plays the move", "[unit][app]") {
  auto s = open("standard");
  REQUIRE(s->applyScript("click e2\nclick e4").has_value());
  REQUIRE(s->game().plyCount() == 1);
  REQUIRE(s->selected() == kInvalidCell);
  REQUIRE(s->snapshot().highlighted().empty());
  REQUIRE(s->game().position().sideToMove() == Color::Black);
  REQUIRE(s->message() == "e2e4");
}

TEST_CASE("clicks that are not moves behave the way a board does", "[unit][app]") {
  auto s = open("standard");

  SECTION("clicking an enemy piece with nothing selected selects nothing") {
    REQUIRE(s->applyScript("click e7").has_value());
    REQUIRE(s->selected() == kInvalidCell);
    REQUIRE(s->game().plyCount() == 0);
  }
  SECTION("clicking an empty cell with nothing selected does nothing") {
    REQUIRE(s->applyScript("click e5").has_value());
    REQUIRE(s->selected() == kInvalidCell);
  }
  SECTION("clicking the selected piece again deselects it") {
    REQUIRE(s->applyScript("click e2\nclick e2").has_value());
    REQUIRE(s->selected() == kInvalidCell);
    REQUIRE(s->game().plyCount() == 0);
  }
  SECTION("clicking another of your own pieces reselects") {
    REQUIRE(s->applyScript("click e2\nclick d2").has_value());
    REQUIRE(s->selected() == cell(*s, "d2"));
    REQUIRE(s->game().plyCount() == 0);
  }
  SECTION("clicking an illegal destination selects nothing and plays nothing") {
    REQUIRE(s->applyScript("click e2\nclick e5").has_value());
    REQUIRE(s->game().plyCount() == 0);
    REQUIRE(s->selected() == kInvalidCell);
  }
}

TEST_CASE("undo walks the game back", "[unit][app]") {
  auto s = open("standard");
  const std::uint64_t startHash = s->game().position().hash();
  REQUIRE(s->applyScript("click e2\nclick e4\nclick e7\nclick e5").has_value());
  REQUIRE(s->game().plyCount() == 2);
  REQUIRE(s->applyScript("undo\nundo").has_value());
  REQUIRE(s->game().plyCount() == 0);
  // Exactly back where it started, hash included - undo is not an approximation.
  REQUIRE(s->game().position().hash() == startHash);
  REQUIRE(s->applyScript("undo").has_value());
  REQUIRE(s->message() == "nothing to undo");
}

TEST_CASE("promotion follows the stated preference", "[unit][app]") {
  // A bare position with one white pawn a step from promoting.
  const char* kAboutToPromote = "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1";

  SECTION("the default is the last declared piece, which is the queen") {
    auto s = open("standard");
    REQUIRE(s->loadFen(kAboutToPromote).has_value());
    REQUIRE(s->applyScript("click b7\nclick b8").has_value());
    REQUIRE(s->game().plyCount() == 1);
    REQUIRE(s->game().position().at(cell(*s, "b8")).type == s->variant().findPiece("queen"));
  }

  SECTION("an explicit preference is honoured") {
    auto s = open("standard");
    REQUIRE(s->loadFen(kAboutToPromote).has_value());
    REQUIRE(s->applyScript("promote knight\nclick b7\nclick b8").has_value());
    REQUIRE(s->game().position().at(cell(*s, "b8")).type == s->variant().findPiece("knight"));
  }

  SECTION("all four promotions are offered as one highlighted destination") {
    auto s = open("standard");
    REQUIRE(s->loadFen(kAboutToPromote).has_value());
    REQUIRE(s->applyScript("click b7").has_value());
    // Four legal moves land on b8, but a player sees one square to click.
    REQUIRE(s->snapshot().highlighted().size() == 1);
    int landing = 0;
    for (const Move& m : s->game().legalMoves()) {
      if (m.to == cell(*s, "b8")) ++landing;
    }
    REQUIRE(landing == 4);
  }
}

TEST_CASE("a click resolved from a pixel reaches the right cell", "[unit][app]") {
  // The full path a mouse takes: pixel -> pick ray -> cell -> action.
  auto s = open("standard");
  const auto& placements = s->placements();

  // Find where e2 is drawn, project it, and click that pixel.
  const CellId e2 = cell(*s, "e2");
  const view::Placement* target = nullptr;
  for (const view::Placement& p : placements) {
    if (p.cell == e2) target = &p;
  }
  REQUIRE(target != nullptr);

  constexpr float kW = 800.0f;
  constexpr float kH = 600.0f;
  const view::Mat4 vp = s->camera().viewProj(kW / kH);
  const float cx = vp[0] * target->x + vp[4] * target->y + vp[8] * target->z + vp[12];
  const float cy = vp[1] * target->x + vp[5] * target->y + vp[9] * target->z + vp[13];
  const float cw = vp[3] * target->x + vp[7] * target->y + vp[11] * target->z + vp[15];
  const float px = (cx / cw * 0.5f + 0.5f) * kW - 0.5f;
  const float py = (cy / cw * 0.5f + 0.5f) * kH - 0.5f;

  REQUIRE(s->clickPixel(px, py, kW, kH) == e2);
  REQUIRE(s->selected() == e2);

  // And a click into empty space deselects rather than erroring.
  REQUIRE(s->clickPixel(1.0f, 1.0f, kW, kH) == kInvalidCell);
  REQUIRE(s->selected() == kInvalidCell);
}

TEST_CASE("the view can be re-aimed at other axes", "[unit][app]") {
  auto s = open("cube5");
  REQUIRE(s->viewConfig().screenAxes.size() == 3);
  REQUIRE(s->applyScript("axes file,level").has_value());
  REQUIRE(s->viewConfig().screenAxes.size() == 2);
  REQUIRE(s->viewConfig().gridAxes.size() == 1);
  // Re-aiming re-lays out the board and re-frames the camera, so every cell is still
  // placed exactly once.
  REQUIRE(s->placements().size() == s->variant().dims.cellCount());

  REQUIRE_FALSE(s->applyScript("axes file,nosuchaxis").has_value());
}

TEST_CASE("camera actions stay within sane limits", "[unit][app]") {
  auto s = open("standard");
  const float startPitch = s->camera().pitch;
  REQUIRE(s->applyScript("orbit 0.2 0.1").has_value());
  REQUIRE(s->camera().pitch > startPitch);
  // Pitch is clamped, so no amount of dragging can turn the board inside out.
  REQUIRE(s->applyScript("orbit 0 100\norbit 0 100").has_value());
  REQUIRE(s->camera().pitch <= 1.5f);
  REQUIRE(s->applyScript("orbit 0 -1000").has_value());
  REQUIRE(s->camera().pitch >= -1.5f);

  const float startDistance = s->camera().distance;
  REQUIRE(s->applyScript("zoom 0.5").has_value());
  REQUIRE(s->camera().distance < startDistance);
}

TEST_CASE("every shipped variant is playable through the session", "[unit][app]") {
  // The interaction layer must not assume two dimensions, a box board, or pawns.
  for (const char* name : {"standard", "cylinder", "torus", "mobius", "klein", "mirrorbox",
                           "cube5", "hyper4", "torus3d"}) {
    CAPTURE(name);
    auto s = open(name);
    REQUIRE(s->game().result() == GameResult::InProgress);

    // Play the first legal move by clicking it, whatever the board looks like.
    const auto& moves = s->game().legalMoves();
    REQUIRE_FALSE(moves.empty());
    Action pick;
    pick.kind = ActionKind::ClickCell;
    pick.cell = moves.front().from;
    REQUIRE(s->apply(pick).has_value());
    REQUIRE(s->selected() == moves.front().from);
    REQUIRE_FALSE(s->snapshot().highlighted().empty());

    Action go;
    go.kind = ActionKind::ClickCell;
    go.cell = s->snapshot().highlighted().front();
    REQUIRE(s->apply(go).has_value());
    REQUIRE(s->game().plyCount() == 1);
    REQUIRE_FALSE(s->statusLine().empty());
  }
}

TEST_CASE("a malformed action script is rejected with its line number", "[unit][app]") {
  auto s = open("standard");
  const auto r = s->applyScript("click e2\nwaggle\n");
  REQUIRE_FALSE(r.has_value());
  REQUIRE(r.error().line == 2);
  REQUIRE(r.error().message.find("waggle") != std::string::npos);
}
