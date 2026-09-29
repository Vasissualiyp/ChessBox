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
