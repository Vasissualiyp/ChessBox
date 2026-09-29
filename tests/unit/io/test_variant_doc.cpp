// SPDX-License-Identifier: GPL-3.0-or-later
//
// M7.0.2: the authoring document and its canonical serializer. The writer is judged by
// identity, not by text: a variant rewritten through the document must still load to
// the same VariantId, and writing what was just written must be a fixed point.
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "io/variant_doc.hpp"
#include "io/variant_toml.hpp"
#include "support/variants.hpp"

using namespace cb;

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

TEST_CASE("a variant survives a parse and rewrite with its identity intact",
          "[unit][io]") {
  for (const char* name : {"standard", "cylinder", "torus", "mobius", "klein",
                           "mirrorbox", "cube5", "hyper4", "torus3d", "t6", "atomic",
                           "atomic_torus", "mustcapture", "charged", "5d"}) {
    CAPTURE(name);
    const std::string source = readFile(test::variantPath(name));

    auto doc = VariantDoc::parse(source, name);
    REQUIRE(doc.has_value());
    const std::string rewritten = doc->serialize();

    auto before = loadVariantToml(source, name);
    auto after = loadVariantToml(rewritten, name);
    REQUIRE(before.has_value());
    REQUIRE(after.has_value());
    // Semantics are what a save must not change. The writer may reflow the text.
    CHECK(before->variantId() == after->variantId());

    // The writer is a fixed point: writing what it wrote changes nothing.
    auto again = VariantDoc::parse(rewritten, name);
    REQUIRE(again.has_value());
    CHECK(again->serialize() == rewritten);
  }
}

TEST_CASE("the document refuses a malformed file where the loader would", "[unit][io]") {
  auto doc = VariantDoc::parse("name = \"x\"\nthis is not toml", "bad");
  REQUIRE_FALSE(doc.has_value());
  CHECK(doc.error().code == ErrorCode::ParseError);
}

TEST_CASE("the document reads and edits a piece's moves", "[unit][io]") {
  auto doc = VariantDoc::parse(readFile(test::variantPath("standard")), "standard");
  REQUIRE(doc.has_value());

  const auto names = doc->pieceNames();
  REQUIRE(std::find(names.begin(), names.end(), "rook") != names.end());

  auto rook = doc->pieceAtoms("rook");
  REQUIRE(rook.has_value());
  REQUIRE(rook->size() == 1);
  CHECK((*rook)[0].maxK == kUnlimited);  // a rider

  auto knight = doc->pieceAtoms("knight");
  REQUIRE(knight.has_value());
  REQUIRE(knight->size() == 1);

  // A rook that can also leap like a knight is a semantic change: it must survive the
  // round trip and it must change the variant's identity.
  auto edited = *rook;
  edited.push_back((*knight)[0]);
  REQUIRE(doc->setPieceAtoms("rook", edited).has_value());

  const auto before =
      loadVariantToml(readFile(test::variantPath("standard")), "standard");
  const auto after = loadVariantToml(doc->serialize(), "standard");
  REQUIRE(before.has_value());
  REQUIRE(after.has_value());
  CHECK(before->variantId() != after->variantId());

  const PieceTypeId rookType = after->findPiece("rook");
  REQUIRE(rookType != kNoPiece);
  CHECK(after->pieces[rookType].atoms.size() == 2);

  // Reading back through the document gives what was written.
  auto reread = doc->pieceAtoms("rook");
  REQUIRE(reread.has_value());
  CHECK(reread->size() == 2);
}

TEST_CASE("a cosmetic edit leaves the variant's identity alone", "[unit][io]") {
  auto doc = VariantDoc::parse(readFile(test::variantPath("standard")), "standard");
  REQUIRE(doc.has_value());
  const auto before = loadVariantToml(doc->serialize(), "standard");
  REQUIRE(before.has_value());

  doc->setDescription("a different pitch for the same game");
  CHECK(doc->description() == "a different pitch for the same game");

  const auto after = loadVariantToml(doc->serialize(), "standard");
  REQUIRE(after.has_value());
  CHECK(before->variantId() == after->variantId());
}
