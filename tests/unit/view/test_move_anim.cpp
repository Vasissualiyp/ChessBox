// SPDX-License-Identifier: GPL-3.0-or-later
//
// The animation is not decoration: it is the only place a player sees *how* a move
// got where it got. On a glued board that is the difference between "the rook teleported"
// and "the rook left here and came back in there", so the route is traced from the
// variant's own atoms and pinned here.
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "io/fen.hpp"
#include "support/variants.hpp"
#include "view/move_anim.hpp"

using namespace cb;
using namespace cb::view;

namespace {

Move slide(const VariantSpec& v, std::initializer_list<int> from,
           std::initializer_list<int> to) {
  Move m;
  m.from = v.dims.toCell(Coord::of(from));
  m.to = v.dims.toCell(Coord::of(to));
  return m;
}

/// The position a trace is asked about: a slide cannot pass through a piece, so the
/// route depends on what else is on the board.
Position board(const VariantSpec& v, const char* fen) {
  auto p = fromFen(v, fen);
  REQUIRE(p.has_value());
  return std::move(*p);
}

/// Run an animation until its portal(s) are open, and hand them back.
std::vector<MoveAnimation::Portal> portalsDuring(MoveAnimation& anim) {
  for (int i = 0; i < 500 && anim.active(); ++i) {
    std::vector<MoveAnimation::Portal> open = anim.openPortals();
    if (!open.empty()) return open;
    anim.advance(0.005f);
  }
  return {};
}

}  // namespace

TEST_CASE("a rook's route is the line it travelled", "[unit][view]") {
  const VariantSpec v = test::loadVariant("standard");
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                               Color::White, slide(v, {0, 0}, {0, 4}));
  REQUIRE_FALSE(p.unexplained);
  CHECK_FALSE(p.leap);
  REQUIRE(p.steps.size() == 4);
  for (const PathStep& s : p.steps) CHECK(s.kind == StepKind::Interior);
  CHECK(p.steps.back().to == p.to);
}

TEST_CASE("a knight arrives without touching what is between", "[unit][view]") {
  const VariantSpec v = test::loadVariant("standard");
  const PieceTypeId knight = v.findPiece("knight");
  const MovePath p = tracePath(v, Position::startPosition(v), knight, Color::White,
                               slide(v, {1, 0}, {2, 2}));
  REQUIRE_FALSE(p.unexplained);
  CHECK(p.leap);
  CHECK(p.steps.size() == 1);
  // A box has no seam anywhere near this move, so nothing opens.
  CHECK(p.steps[0].kind == StepKind::Interior);
}

TEST_CASE("a leaping move off a mirror bounces instead of opening a portal",
          "[unit][view]") {  // A knight on g4 sent at the h-file reflects off it and
                             // lands on h5. The old rule
  // called any boundary step that did not land next door a portal, so this - and every
  // leaper off a mirror - drew a portal opening beside a piece in the middle of the
  // board.
  const VariantSpec v = test::loadVariant("mirrorbox");
  const PieceTypeId knight = v.findPiece("knight");
  const MovePath p = tracePath(v, board(v, "8/8/8/8/6N1/8/8/8 w - - 0 1"), knight,
                               Color::White, slide(v, {6, 3}, {7, 4}));
  REQUIRE_FALSE(p.unexplained);
  bool bounced = false;
  for (const PathStep& s : p.steps) {
    CHECK(s.kind != StepKind::Portal);  // a reflecting wall is not a portal
    if (s.kind == StepKind::Bounce) bounced = true;
  }
  CHECK(bounced);
}

TEST_CASE("a pawn capturing off a mirror bounces at the wall", "[unit][view]") {
  // A white pawn on h4 takes the black pawn on h5 by reflecting off the h-file, because
  // its only legal capture direction leaves the board. The pawn's push reaches h5 too -
  // but it cannot land on a piece, so it must not be chosen for a capture; otherwise the
  // animation showed a straight step where the piece actually bounced.
  const VariantSpec v = test::loadVariant("mirrorbox");
  const PieceTypeId pawn = v.findPiece("pawn");
  const MovePath p = tracePath(v, board(v, "8/8/8/7p/7P/8/8/8 w - - 0 1"), pawn,
                               Color::White, slide(v, {7, 3}, {7, 4}));
  REQUIRE_FALSE(p.unexplained);
  bool bounced = false;
  for (const PathStep& s : p.steps) {
    CHECK(s.kind != StepKind::Portal);
    if (s.kind == StepKind::Bounce) bounced = true;
  }
  CHECK(bounced);
}

TEST_CASE("a move nothing in the variant explains is reported, not invented",
          "[unit][view]") {
  const VariantSpec v = test::loadVariant("standard");
  const PieceTypeId rook = v.findPiece("rook");
  // A rook cannot reach this square by any atom it has.
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                               Color::White, slide(v, {0, 0}, {3, 5}));
  CHECK(p.unexplained);
}

TEST_CASE("crossing a gluing is a portal, not a longer line", "[unit][view]") {
  const VariantSpec v = test::loadVariant("cylinder");
  const PieceTypeId rook = v.findPiece("rook");
  // Two squares to the left from the a-file leaves the board and comes back on the
  // g-file.
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                               Color::White, slide(v, {0, 0}, {6, 0}));
  REQUIRE_FALSE(p.unexplained);
  REQUIRE(p.steps.size() == 2);
  CHECK(p.steps[0].kind == StepKind::Portal);
  CHECK(p.steps[1].kind == StepKind::Interior);
  CHECK(p.steps[0].to == v.dims.toCell(Coord::of({7, 0})));
}

TEST_CASE("a blocked straight ray is not the route the piece took", "[unit][view]") {
  // A rook on a1 with a piece on c1 can still reach d1 - the long way, out through the
  // file seam. The straight ray the atom table lists first passes *through* the blocker,
  // which no real move does, so occupancy has to choose the route, not atom order.
  const VariantSpec v = test::loadVariant("cylinder");
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/R1P5 w - - 0 1"), rook,
                               Color::White, slide(v, {0, 0}, {3, 0}));
  REQUIRE_FALSE(p.unexplained);
  bool crossed = false;
  for (const PathStep& s : p.steps) {
    CHECK(s.to != v.dims.toCell(Coord::of({2, 0})));  // never the blocker
    if (s.kind == StepKind::Portal) crossed = true;
  }
  CHECK(crossed);
}

TEST_CASE("a portal stands across the axis the piece crossed", "[unit][view]") {
  // Cylinder glues the files, so a piece leaving the a-file travels along the file axis
  // (screen X) and the portal must be a slab across *that* axis, not along it: a portal
  // lying flat on the file would point the piece's exit the wrong way.
  const VariantSpec v = test::loadVariant("cylinder");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const Theme theme = Theme::console();
  const SeamMap seams = SeamMap::build(v, cfg, theme);
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                               Color::White, slide(v, {0, 0}, {6, 0}));

  MoveAnimation anim;
  anim.start(cfg, layout(v.dims, cfg), seams, theme, p, 0.12f);
  const std::vector<MoveAnimation::Portal> portals = portalsDuring(anim);
  REQUIRE_FALSE(portals.empty());
  for (const MoveAnimation::Portal& portal : portals) {
    CHECK(std::abs(portal.nx) > std::abs(portal.ny));
  }
}

TEST_CASE("a diagonal wrap faces the edge crossed, not the direction's lean",
          "[unit][view]") {
  // A bishop leaving a1 up-left wraps to h2. Its travel direction is a perfect diagonal,
  // so a portal oriented by the direction has no axis to prefer - it must be oriented by
  // the file edge the move actually went through.
  const VariantSpec v = test::loadVariant("cylinder");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const Theme theme = Theme::console();
  const SeamMap seams = SeamMap::build(v, cfg, theme);
  const PieceTypeId bishop = v.findPiece("bishop");
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/B7 w - - 0 1"), bishop,
                               Color::White, slide(v, {0, 0}, {7, 1}));
  REQUIRE_FALSE(p.unexplained);

  MoveAnimation anim;
  anim.start(cfg, layout(v.dims, cfg), seams, theme, p, 0.12f);
  const std::vector<MoveAnimation::Portal> portals = portalsDuring(anim);
  REQUIRE_FALSE(portals.empty());
  for (const MoveAnimation::Portal& portal : portals) {
    CHECK(std::abs(portal.nx) > std::abs(portal.ny));  // across the file
  }
}

TEST_CASE("a rank wrap faces the rank even when the move leans along the file",
          "[unit][view]") {
  // A knight on d8 sent two files across and one rank up leaves through the *rank* edge
  // and re-enters at c1. The direction leans along the file (2 against 1), so orienting
  // the portal by the direction put it on the wrong edge; it has to follow the face.
  const VariantSpec v = test::loadVariant("klein");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const Theme theme = Theme::console();
  const SeamMap seams = SeamMap::build(v, cfg, theme);
  const PieceTypeId knight = v.findPiece("knight");
  const MovePath p = tracePath(v, board(v, "3N4/8/8/8/8/8/8/8 w - - 0 1"), knight,
                               Color::White, slide(v, {3, 7}, {2, 0}));
  REQUIRE_FALSE(p.unexplained);
  REQUIRE(p.steps.size() == 1);
  CHECK(p.steps[0].faceAxis == 1);  // the rank edge

  MoveAnimation anim;
  anim.start(cfg, layout(v.dims, cfg), seams, theme, p, 0.12f);
  const std::vector<MoveAnimation::Portal> portals = portalsDuring(anim);
  REQUIRE_FALSE(portals.empty());
  for (const MoveAnimation::Portal& portal : portals) {
    CHECK(std::abs(portal.ny) > std::abs(portal.nx));  // across the rank
  }
}

TEST_CASE("a portal opens on the board edge, not at the cell it left", "[unit][view]") {
  // A wrapping move leaves through the boundary, so both ends of the portal sit on the
  // board's edge plane - not half a cell from the centre of the piece that left, which
  // is where they used to be drawn.
  const VariantSpec v = test::loadVariant("cylinder");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const Theme theme = Theme::console();
  const SeamMap seams = SeamMap::build(v, cfg, theme);
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                               Color::White, slide(v, {0, 0}, {6, 0}));
  const Bounds b = boundsOf(layout(v.dims, cfg));

  MoveAnimation anim;
  anim.start(cfg, layout(v.dims, cfg), seams, theme, p, 0.12f);
  const std::vector<MoveAnimation::Portal> portals = portalsDuring(anim);
  REQUIRE(portals.size() >= 2);
  bool atMinEdge = false;
  bool atMaxEdge = false;
  for (const MoveAnimation::Portal& portal : portals) {
    if (std::abs(portal.x - (b.minX - 0.5f)) < 0.01f) atMinEdge = true;
    if (std::abs(portal.x - (b.maxX + 0.5f)) < 0.01f) atMaxEdge = true;
  }
  CHECK(atMinEdge);
  CHECK(atMaxEdge);
}

TEST_CASE("a mirror deflects the ray rather than moving it elsewhere", "[unit][view]") {
  const VariantSpec v = test::loadVariant("mirrorbox");
  const PieceTypeId bishop = v.findPiece("bishop");
  // f2 up-and-right reaches the h-wall at h4, is turned around there, and comes back
  // down the other diagonal to f7 - a square no bishop could reach from f2 on a plain
  // board, so the route has to go through the wall to exist at all.
  const MovePath p = tracePath(v, Position::startPosition(v), bishop, Color::White,
                               slide(v, {5, 1}, {5, 6}));
  REQUIRE_FALSE(p.unexplained);
  CHECK_FALSE(p.leap);
  REQUIRE(p.steps.size() == 5);
  CHECK(p.steps[0].kind == StepKind::Interior);  // g3
  CHECK(p.steps[1].kind == StepKind::Interior);  // h4
  CHECK(p.steps[2].kind == StepKind::Bounce);    // turned around against the h-wall
  CHECK(p.steps[3].kind == StepKind::Interior);  // g6
  CHECK(p.steps[4].kind == StepKind::Interior);  // f7
  // A deflection is not a portal: nothing opens, because nothing is on the far side.
  for (const PathStep& s : p.steps) CHECK(s.kind != StepKind::Portal);
}

TEST_CASE("the animation runs, breaks at a portal, and ends on the target",
          "[unit][view]") {
  const VariantSpec v = test::loadVariant("cylinder");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const auto placements = layout(v.dims, cfg);
  const Theme theme = Theme::console();
  const SeamMap seams = SeamMap::build(v, cfg, theme);
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                               Color::White, slide(v, {0, 0}, {6, 0}));

  MoveAnimation anim;
  anim.start(cfg, placements, seams, theme, p, 0.12f);
  REQUIRE(anim.active());

  bool sawPortal = false;
  for (int i = 0; i < 200 && anim.active(); ++i) {
    if (!anim.openPortals().empty()) sawPortal = true;
    anim.advance(0.01f);
  }
  CHECK(sawPortal);
  CHECK_FALSE(anim.active());
}

TEST_CASE("an ordinary move opens no portal", "[unit][view]") {
  const VariantSpec v = test::loadVariant("standard");
  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const auto placements = layout(v.dims, cfg);
  const Theme theme = Theme::console();
  const SeamMap seams = SeamMap::build(v, cfg, theme);
  const PieceTypeId rook = v.findPiece("rook");
  const MovePath p = tracePath(v, board(v, "8/8/8/8/8/8/8/R7 w - - 0 1"), rook,
                               Color::White, slide(v, {0, 0}, {0, 4}));

  MoveAnimation anim;
  anim.start(cfg, placements, seams, theme, p, 0.12f);
  while (anim.active()) {
    CHECK(anim.openPortals().empty());
    anim.advance(0.02f);
  }
}

TEST_CASE("an en-passant capture animates like the move it is", "[unit][view]") {
  // The capturing pawn steps diagonally onto an empty square while the pawn it takes
  // stands beside it. The route is the pawn's own capture atom - and getting this wrong
  // crashed the game, because a path with no steps left the animation with a one-point
  // polyline to interpolate along.
  const VariantSpec v = test::loadVariant("standard");
  const PieceTypeId pawn = v.findPiece("pawn");
  Move m;
  m.from = v.dims.toCell(Coord::of({4, 4}));   // e5
  m.to = v.dims.toCell(Coord::of({3, 5}));     // d6
  m.captureCell = v.dims.toCell(Coord::of({3, 4}));  // the pawn on d5
  m.flags = MoveFlag::Capture | MoveFlag::EnPassant;

  // White pawn on e5, black pawn that has just stepped two squares to d5.
  const Position pos = board(v, "8/8/8/3pP3/8/8/8/8 w - d6 0 1");
  const MovePath p = tracePath(v, pos, pawn, Color::White, m);
  REQUIRE_FALSE(p.unexplained);
  REQUIRE(p.steps.size() == 1);
  CHECK(p.steps[0].to == m.to);

  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const auto placements = layout(v.dims, cfg);
  const Theme theme = Theme::manifold();
  const SeamMap seams = SeamMap::build(v, cfg, theme);
  MoveAnimation anim;
  anim.start(cfg, placements, seams, theme, p, 0.12f);
  while (anim.active()) {
    (void)anim.sample();
    anim.advance(0.01f);
  }
}

TEST_CASE("a move no atom explains is animated, not crashed on", "[unit][view]") {
  // Castling, a rule effect that displaces a piece, a variant whose atoms were edited
  // under a running game: whatever the cause, the animation has to cope. A route with no
  // steps is a straight glide from one cell to the other, never a one-point polyline.
  const VariantSpec v = test::loadVariant("standard");
  const PieceTypeId rook = v.findPiece("rook");
  const Position pos = board(v, "8/8/8/8/8/8/8/R7 w - - 0 1");
  const MovePath p = tracePath(v, pos, rook, Color::White, slide(v, {0, 0}, {3, 5}));
  REQUIRE(p.unexplained);

  const ViewConfig cfg = ViewConfig::forBoard(v.dims);
  const auto placements = layout(v.dims, cfg);
  const Theme theme = Theme::manifold();
  const SeamMap seams = SeamMap::build(v, cfg, theme);
  MoveAnimation anim;
  anim.start(cfg, placements, seams, theme, p, 0.12f);
  // It either glides or does nothing, but it never reads past the end of a run.
  for (int i = 0; i < 400 && anim.active(); ++i) {
    const MoveAnimation::Sample at = anim.sample();
    CHECK(std::isfinite(at.x));
    CHECK(std::isfinite(at.y));
    anim.advance(0.01f);
  }
}
