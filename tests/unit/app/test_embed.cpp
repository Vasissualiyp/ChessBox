// SPDX-License-Identifier: GPL-3.0-or-later
//
// M13.9 part F: `embedOnto`, the pure helper that carries a position across a variant
// switch. Two cases are the whole contract: embedding onto a board of the same shape
// leaves every piece exactly where it was, and embedding onto a board with a new axis
// puts every piece on slice 0 of that axis and leaves the rest of it empty.
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

#include "app/embed.hpp"
#include "position/position.hpp"
#include "support/variants.hpp"
#include "variant/variant.hpp"

using namespace cb;
using namespace cb::app;

namespace {

std::string_view pieceName(const VariantSpec& v, const Piece& p) {
  return p.empty() ? std::string_view{} : v.pieces[p.type].name;
}

}  // namespace

TEST_CASE("embedOnto is the identity across two variants of the same shape",
          "[unit][app][attract]") {
  const VariantSpec from = test::loadVariant("standard");
  const VariantSpec to = test::loadVariant("cylinder");
  const Position source = Position::startPosition(from);
  const Position embedded = embedOnto(source, from, to);

  REQUIRE(embedded.variant().name == "cylinder");
  REQUIRE(embedded.cellCount() == source.cellCount());
  CHECK(embedded.sideToMove() == source.sideToMove());
  for (CellId c = 0; c < source.cellCount(); ++c) {
    CHECK(embedded.isEmpty(c) == source.isEmpty(c));
    if (source.isEmpty(c)) continue;
    CHECK(pieceName(to, embedded.at(c)) == pieceName(from, source.at(c)));
    CHECK(embedded.at(c).colorOf() == source.at(c).colorOf());
  }
}

TEST_CASE("embedOnto fixes a newly-introduced axis at 0 for every piece",
          "[unit][app][attract]") {
  const VariantSpec from = test::loadVariant("standard");  // file, rank
  const VariantSpec to = test::loadVariant("hyper4_8x8");  // file, rank, level, aeon
  const Position source = Position::startPosition(from);
  const Position embedded = embedOnto(source, from, to);

  REQUIRE(embedded.variant().name == "hyper4_8x8");
  REQUIRE(embedded.cellCount() == 512);
  CHECK(embedded.sideToMove() == source.sideToMove());

  // The whole of each new axis except slice 0 is empty, and the pieces that were on the
  // source board are all still there, on the same file and rank.
  std::size_t sourceCount = 0;
  std::size_t embeddedCount = 0;
  for (CellId c = 0; c < source.cellCount(); ++c) {
    if (!source.isEmpty(c)) ++sourceCount;
  }
  for (CellId c = 0; c < embedded.cellCount(); ++c) {
    if (embedded.isEmpty(c)) continue;
    ++embeddedCount;
    const Coord p = to.dims.toCoord(c);
    CHECK(p[2] == 0);  // level
    CHECK(p[3] == 0);  // aeon
  }
  CHECK(embeddedCount == sourceCount);

  // And each source piece is found at its own coordinate on slice 0.
  for (CellId c = 0; c < source.cellCount(); ++c) {
    if (source.isEmpty(c)) continue;
    const Coord p = from.dims.toCoord(c);
    Coord q(to.dims.dims());
    q[0] = p[0];
    q[1] = p[1];
    const CellId target = to.dims.toCell(q);
    CHECK_FALSE(embedded.isEmpty(target));
    CHECK(pieceName(to, embedded.at(target)) == pieceName(from, source.at(c)));
    CHECK(embedded.at(target).colorOf() == source.at(c).colorOf());
  }
}
