// SPDX-License-Identifier: GPL-3.0-or-later
// The loader's fidelity test. variants/standard.toml and makeStandardChess() are
// two independent descriptions of the same game; if they produce the same
// variantId then the data path and the code path cannot have drifted, and the
// hash covers every field that affects play (M1.7).
#include <catch2/catch_test_macros.hpp>

#include "io/fen.hpp"
#include "io/variant_toml.hpp"
#include "movegen/movegen.hpp"
#include "support/variants.hpp"
#include "variant/standard.hpp"

using namespace cb;

TEST_CASE("standard.toml and the programmatic variant are identical", "[golden][io]") {
  const auto code = makeStandardChess();
  REQUIRE(code.has_value());
  const auto data = loadVariantFile(test::variantPath("standard"));
  REQUIRE(data.has_value());

  REQUIRE(data->variantId() == code->variantId());
  REQUIRE(data->dirTable.size() == code->dirTable.size());
  REQUIRE(data->pieces.size() == code->pieces.size());
  REQUIRE(data->start.size() == code->start.size());

  // And they play the same: the strongest form of the claim.
  Position pd = Position::startPosition(*data);
  Position pc = Position::startPosition(*code);
  REQUIRE(pd.hash() == pc.hash());
  REQUIRE(toFen(pd) == toFen(pc));
  const MoveGen gd(*data);
  const MoveGen gc(*code);
  REQUIRE(gd.perft(pd, 3) == gc.perft(pc, 3));
}

TEST_CASE("the shipped standard.toml starts from the expected FEN", "[golden][io]") {
  const auto v = test::loadVariant("standard");
  const Position p = Position::startPosition(v);
  REQUIRE(toFen(p) == "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
}

TEST_CASE("a malformed variant file yields a diagnostic, never a crash", "[golden][io]") {
  // Every rejection names what is wrong in terms an author can act on. The line
  // number matters as much as the message.
  struct Case {
    const char* toml;
    const char* expectFragment;
  };
  const Case cases[] = {
      {"", "name"},
      {"name = \"x\"\n", "[[axis]]"},
      {"name = \"x\"\n[[axis]]\nname=\"a\"\nextent=0\n", "non-positive"},
      {"name = \"x\"\n[[axis]]\nname=\"a\"\nextent=8\n", "[[piece]]"},
      {"name=\"x\"\n[[axis]]\nname=\"a\"\nextent=8\n[[piece]]\nname=\"p\"\nsymbol=\"P\"\n",
       "[[piece.move]]"},
      {"name=\"x\"\n[[axis]]\nname=\"a\"\nextent=8\n[[piece]]\nname=\"p\"\nsymbol=\"PP\"\n"
       "[[piece.move]]\nvector=[1]\n",
       "single-character"},
      {"name=\"x\"\n[[axis]]\nname=\"a\"\nextent=8\n[[piece]]\nname=\"p\"\nsymbol=\"P\"\n"
       "[[piece.move]]\nvector=[1]\nmode=\"teleport\"\n",
       "'slide', 'leap' or 'hop'"},
      {"name=\"x\"\n[[axis]]\nname=\"a\"\nextent=8\n[[piece]]\nname=\"p\"\nsymbol=\"P\"\n"
       "[[piece.move]]\nvector=[1]\nforward=true\n",
       "orientation axis"},
      {"name=\"x\"\n[[axis]]\nname=\"a\"\nextent=8\n[[piece]]\nname=\"p\"\nsymbol=\"P\"\n"
       "[[piece.move]]\nvector=[0]\n",
       "positive"},
      {"name=\"x\"\n[[axis]]\nname=\"a\"\nextent=4\n[[geometry.identify]]\naxis=\"nope\"\n"
       "[[piece]]\nname=\"p\"\nsymbol=\"P\"\n[[piece.move]]\nvector=[1]\n",
       "not declared"},
      {"name=\"x\" this is not toml", "\n"},
  };
  for (const Case& c : cases) {
    CAPTURE(c.toml);
    const auto r = loadVariantToml(c.toml, "<test>");
    REQUIRE_FALSE(r.has_value());
    REQUIRE_FALSE(r.error().message.empty());
    if (std::string_view(c.expectFragment) != "\n") {
      CAPTURE(r.error().format());
      REQUIRE(r.error().message.find(c.expectFragment) != std::string::npos);
    }
  }
}

TEST_CASE("a variant with a missing starting board is rejected", "[golden][io]") {
  const auto r = loadVariantToml(
      "name=\"x\"\n[[axis]]\nname=\"a\"\nextent=4\n[[piece]]\nname=\"p\"\nsymbol=\"P\"\n"
      "[[piece.move]]\nvector=[1]\n");
  REQUIRE_FALSE(r.has_value());
  REQUIRE(r.error().message.find("start") != std::string::npos);
}
