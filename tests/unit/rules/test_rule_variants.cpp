// SPDX-License-Identifier: GPL-3.0-or-later
// The rule VM, exercised through the variants that use it.
//
// Each test names the mechanism it covers, because the point of these variants is not
// that they are fun - it is that each one is a different part of the VM, proved to work
// from a data file with no engine code behind it.
#include <catch2/catch_test_macros.hpp>

#include "game/game.hpp"
#include "io/fen.hpp"
#include "io/notation.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

/// A game set up from FEN. The variant is kept alive by the caller.
Game gameFrom(const VariantSpec& v, std::string_view fen) {
  Game g(v);
  auto p = fromFen(v, fen);
  REQUIRE(p.has_value());
  g.position() = std::move(*p);
  return g;
}

const Move* findMove(const Game& g, const char* from, const char* to) {
  const CellId f = parseCell(g.variant().dims, from).value();
  const CellId t = parseCell(g.variant().dims, to).value();
  for (const Move& m : g.legalMoves()) {
    if (m.from == f && m.to == t) return &m;
  }
  return nullptr;
}

bool occupied(const Game& g, const char* cellName) {
  return !g.position().at(parseCell(g.variant().dims, cellName).value()).empty();
}

}  // namespace

TEST_CASE("atomic: a capture destroys its neighbourhood", "[unit][rules]") {
  // destroy + destroy_region with a filter.
  const VariantSpec v = test::loadVariant("atomic");
  // White knight e4 takes the pawn on d6. A black rook on c7 and a black pawn on c5 are
  // both inside the blast radius; only the rook should die.
  Game g = gameFrom(v, "4k3/2r5/3p4/2p5/4N3/8/8/4K3 w - - 0 1");

  const Move* capture = findMove(g, "e4", "d6");
  REQUIRE(capture != nullptr);
  REQUIRE(g.play(*capture).has_value());

  // The captured pawn and the capturing knight are both gone.
  REQUIRE_FALSE(occupied(g, "d6"));
  REQUIRE_FALSE(occupied(g, "e4"));
  // The rook was inside the radius and is destroyed; the pawn inside it is spared by the
  // filter, because atomic explosions do not touch pawns.
  REQUIRE_FALSE(occupied(g, "c7"));
  REQUIRE(occupied(g, "c5"));
  REQUIRE(g.position().at(parseCell(v.dims, "c5").value()).type == v.findPiece("pawn"));
}

TEST_CASE("atomic: blowing up your own king is not a legal move", "[unit][rules]") {
  // The mechanism worth noticing: nothing in atomic.toml says this. Legality is decided
  // *after* a move's effects run, so a capture that destroys your own royal piece simply
  // never appears among the legal moves.
  const VariantSpec atomic = test::loadVariant("atomic");
  const VariantSpec standard = test::loadVariant("standard");
  const char* kFen = "8/8/8/R7/8/8/p7/1K6 w - - 0 1";

  Game normal = gameFrom(standard, kFen);
  REQUIRE(findMove(normal, "a5", "a2") != nullptr);  // an ordinary rook capture

  Game boom = gameFrom(atomic, kFen);
  // The same capture would explode next to the white king on b1.
  REQUIRE(findMove(boom, "a5", "a2") == nullptr);
}

TEST_CASE("atomic: destroying the enemy king ends the game", "[unit][rules]") {
  const VariantSpec v = test::loadVariant("atomic");
  // Black king b8, black pawn a7 for White's rook to take, White king far away.
  Game g = gameFrom(v, "1k6/p7/R7/8/8/8/8/6K1 w - - 0 1");
  const Move* capture = findMove(g, "a6", "a7");
  REQUIRE(capture != nullptr);
  REQUIRE(g.play(*capture).has_value());

  REQUIRE_FALSE(occupied(g, "b8"));  // the king was inside the blast
  // And the consequence follows from the general rule rather than a special case: Black
  // has no royal piece, so Black has no legal moves, and White has won.
  REQUIRE(g.legalMoves().empty());
  REQUIRE(g.result() == GameResult::WhiteWins);
}

TEST_CASE("mustcapture: a rule can forbid moves before legality", "[unit][rules]") {
  // on_move_filter + any_capture.
  const VariantSpec v = test::loadVariant("mustcapture");
  Game g = gameFrom(v, "4k3/8/3p4/8/4N3/8/8/4K3 w - - 0 1");

  // A capture exists, so every non-capture is forbidden.
  REQUIRE_FALSE(g.legalMoves().empty());
  for (const Move& m : g.legalMoves()) {
    CAPTURE(moveText(v, m));
    REQUIRE(m.captureCell != kInvalidCell);
  }
  REQUIRE(findMove(g, "e4", "d6") != nullptr);

  SECTION("with no capture available, everything is legal again") {
    Game quiet = gameFrom(v, "4k3/8/8/8/4N3/8/8/4K3 w - - 0 1");
    bool sawQuiet = false;
    for (const Move& m : quiet.legalMoves()) {
      if (m.captureCell == kInvalidCell) sawQuiet = true;
    }
    REQUIRE(sawQuiet);
  }
}

TEST_CASE("charged: custom fields travel, hash, and undo exactly", "[unit][rules]") {
  const VariantSpec v = test::loadVariant("charged");
  REQUIRE(v.pieceFields.size() == 1);
  const int charge = v.findPieceField("charge");
  REQUIRE(charge >= 0);

  Game g(v);
  const std::uint64_t startHash = g.position().hash();
  const CellId e2 = parseCell(v.dims, "e2").value();
  const CellId e4 = parseCell(v.dims, "e4").value();
  REQUIRE(g.position().pieceField(charge, e2) == 0);

  REQUIRE(g.play(*findMove(g, "e2", "e4")).has_value());
  // The field followed the piece to its destination and left the origin at default.
  REQUIRE(g.position().pieceField(charge, e4) == 1);
  REQUIRE(g.position().pieceField(charge, e2) == 0);
  // It is part of the position's identity, so the hash moved with it.
  REQUIRE(g.position().hash() != startHash);
  REQUIRE(g.position().hash() == g.position().computeHash());

  REQUIRE(g.undo());
  REQUIRE(g.position().pieceField(charge, e4) == 0);
  REQUIRE(g.position().hash() == startHash);
  std::string why;
  REQUIRE(g.position().validate(&why));
  INFO(why);
}

TEST_CASE("charged: ordered rules transform a piece after three moves", "[unit][rules]") {
  // Two rules fire on the same trigger; the charge is incremented first, so the second
  // rule sees the new value. Rule order is part of the variant's meaning.
  const VariantSpec v = test::loadVariant("charged");
  Game g = gameFrom(v, "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1");
  const auto cellOf = [&](const char* n) { return parseCell(v.dims, n).value(); };

  const char* path[][2] = {{"e2", "e3"}, {"e3", "e4"}, {"e4", "e5"}};
  for (auto& step : path) {
    const Move* m = findMove(g, step[0], step[1]);
    REQUIRE(m != nullptr);
    REQUIRE(g.play(*m).has_value());
    // Black has only king moves; play one so it is White's turn again.
    REQUIRE_FALSE(g.legalMoves().empty());
    REQUIRE(g.play(g.legalMoves().front()).has_value());
  }

  REQUIRE(g.position().pieceField(v.findPieceField("charge"), cellOf("e5")) == 3);
  REQUIRE(g.position().at(cellOf("e5")).type == v.findPiece("knight"));
}

TEST_CASE("atomic on a torus: rules and geometry compose", "[unit][rules]") {
  // The claim this variant exists to test: nobody wrote code to make an explosion wrap.
  // The rule asks the geometry which cells surround a cell, and on a torus the answer
  // includes cells on the far side of the board.
  const VariantSpec v = test::loadVariant("atomic_torus");
  REQUIRE_FALSE(v.geom.isBox());

  // White rook a4 takes a black knight on a5. The black rook on h4 is a neighbour of a4
  // only because the files are glued, so it is inside the blast. Both kings are placed
  // well clear of it - on a torus "clear" means at most four cells away.
  Game g = gameFrom(v, "8/8/8/n2k4/R6r/8/8/3K4 w - - 0 1");
  const Move* capture = findMove(g, "a4", "a5");
  REQUIRE(capture != nullptr);
  REQUIRE(g.play(*capture).has_value());

  REQUIRE_FALSE(occupied(g, "a5"));  // the knight
  REQUIRE_FALSE(occupied(g, "a4"));  // the capturing rook
  // And the rook on h4, which is adjacent to a4 only because the board wraps.
  REQUIRE_FALSE(occupied(g, "h4"));
}

TEST_CASE("rule execution is deterministic and reversible", "[unit][rules]") {
  // Rules mutate the board, so undo has to restore what they did as well as the move
  // itself. Anything less and the search, the replay and the undo button all break.
  for (const char* name : {"atomic", "mustcapture", "charged", "atomic_torus"}) {
    CAPTURE(name);
    const VariantSpec v = test::loadVariant(name);
    Game g(v);
    Rng rng(12345);

    std::vector<std::uint64_t> hashes{g.position().hash()};
    int played = 0;
    for (int ply = 0; ply < 24; ++ply) {
      const auto& moves = g.legalMoves();
      // An explosion can end the game abruptly - that is the variant working, not a
      // failure - so a short playout is fine as long as it unwinds exactly.
      if (moves.empty()) break;
      REQUIRE(g.play(moves[rng.below(static_cast<std::uint32_t>(moves.size()))]).has_value());
      REQUIRE(g.position().hash() == g.position().computeHash());
      hashes.push_back(g.position().hash());
      ++played;
    }
    REQUIRE(played >= 2);

    while (g.plyCount() > 0) {
      REQUIRE(g.undo());
      hashes.pop_back();
      std::string why;
      REQUIRE(g.position().validate(&why));
      INFO(why);
      REQUIRE(g.position().hash() == hashes.back());
    }
  }
}

TEST_CASE("a malformed rule set is rejected with an actionable message",
          "[unit][rules]") {
  const std::string base =
      "name=\"t\"\n[[axis]]\nname=\"file\"\nextent=4\n[[axis]]\nname=\"rank\"\nextent=4\n"
      "[[piece]]\nname=\"king\"\nsymbol=\"K\"\nroyal=true\n[[piece.move]]\nvector=[1]\nmax=1\n"
      "[start]\nboard=\"4/4/4/4\"\n";
  struct Case {
    const char* rules;
    const char* fragment;
  };
  const Case cases[] = {
      {"[[rule]]\nname=\"r\"\nwhen=\"never\"\n[[rule.effect]]\nop=\"destroy\"\nat=\"move.to\"\n",
       "'when' must be one of"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_end\"\n", "at least one"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_end\"\n[[rule.effect]]\nop=\"wiggle\"\n",
       "unknown effect"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_end\"\n[[rule.effect]]\nop=\"destroy\"\n",
       "needs 'at'"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_end\"\n[[rule.effect]]\nop=\"destroy\"\nat=\"(nope)\"\n",
       "unknown operator"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_end\"\n[[rule.effect]]\nop=\"destroy\"\nat=\"piece:dragon\"\n",
       "no piece is called"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_end\"\n[[rule.effect]]\nop=\"transform\"\nat=\"move.to\"\npiece=\"dragon\"\n",
       "no piece is called"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_end\"\n[[rule.effect]]\nop=\"set_piece_field\"\nat=\"move.to\"\nvalue=\"1\"\nfield=\"charge\"\n",
       "no field is called"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_end\"\n[[rule.effect]]\nop=\"forbid_move\"\n",
       "only means anything under on_move_filter"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_move_filter\"\n[[rule.effect]]\nop=\"repeat_turn\"\n",
       "cannot run before the move"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_capture\"\n[[rule.effect]]\nop=\"destroy_region\"\nat=\"move.to\"\nradius=9\n",
       "radius must be between"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_capture\"\n[[rule.effect]]\nop=\"destroy\"\nat=\"cell\"\n",
       "only be used inside an effect that walks cells"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_capture\"\n[[rule.effect]]\nop=\"destroy\"\nat=\"(eq move.to)\"\n",
       "takes 2 argument"},
      {"[[rule]]\nname=\"r\"\nwhen=\"on_capture\"\n[[rule.effect]]\nop=\"destroy\"\nat=\"(coord nosuch move.to)\"\n",
       "does not declare"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.rules);
    const auto r = loadVariantToml(base + c.rules, "<rules>");
    REQUIRE_FALSE(r.has_value());
    CAPTURE(r.error().format());
    REQUIRE(r.error().message.find(c.fragment) != std::string::npos);
  }
}

TEST_CASE("a variant's rules are part of its identity", "[unit][rules]") {
  // Two variants with the same pieces and different rules are different games, and must
  // not share a variantId - multiplayer and replays compare exactly that.
  const VariantSpec standard = test::loadVariant("standard");
  const VariantSpec atomic = test::loadVariant("atomic");
  const VariantSpec mustcapture = test::loadVariant("mustcapture");
  REQUIRE(standard.variantId() != atomic.variantId());
  REQUIRE(atomic.variantId() != mustcapture.variantId());
  REQUIRE(standard.variantId() != mustcapture.variantId());
}

TEST_CASE("declaring fields costs nothing when they are unused", "[unit][rules]") {
  // A variant with no fields allocates no columns at all, which is what keeps the
  // feature free for the variants that do not want it.
  const VariantSpec plain = test::loadVariant("standard");
  REQUIRE(plain.pieceFields.empty());
  REQUIRE(plain.cellFields.empty());

  // And a position in a field-carrying variant hashes like the plain one when every
  // field sits at its default, because only non-default values contribute.
  const VariantSpec charged = test::loadVariant("charged");
  REQUIRE(charged.pieceFields.size() == 1);
  const Position p = Position::startPosition(charged);
  REQUIRE(p.hash() == p.computeHash());
}
