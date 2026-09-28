// SPDX-License-Identifier: GPL-3.0-or-later
#include "base/zobrist.hpp"

#include <set>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Zobrist keys are reproducible from a seed", "[unit][base]") {
  cb::Zobrist a;
  cb::Zobrist b;
  a.init(7, 64, 13, 4);
  b.init(7, 64, 13, 4);
  for (std::uint32_t c = 0; c < 64; ++c) {
    for (std::uint32_t p = 0; p < 13; ++p) REQUIRE(a.piece(c, p) == b.piece(c, p));
  }
  REQUIRE(a.side() == b.side());
}

TEST_CASE("Zobrist keys are distinct", "[unit][base]") {
  cb::Zobrist z;
  z.init(1, 64, 13, 0);
  std::set<std::uint64_t> keys;
  for (std::uint32_t c = 0; c < 64; ++c) {
    for (std::uint32_t p = 0; p < 13; ++p) keys.insert(z.piece(c, p));
  }
  REQUIRE(keys.size() == 64 * 13);  // no collisions in the table itself
}

TEST_CASE("Zobrist neutral states contribute nothing", "[unit][base]") {
  // "No en-passant target" and "no castling rights" must be identity elements,
  // or an empty state would perturb the hash and break FEN round-trips.
  cb::Zobrist z;
  z.init(1, 64, 13, 0);
  REQUIRE(z.epTarget(64) == 0);
  REQUIRE(z.castleRights(0) == 0);
}
