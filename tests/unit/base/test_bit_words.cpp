// SPDX-License-Identifier: GPL-3.0-or-later
#include "base/bit_words.hpp"

#include <bitset>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "base/rng.hpp"

using cb::BitWords;

TEST_CASE("BitWords set, test and clear individual bits", "[unit][base]") {
  BitWords b(200);
  REQUIRE(b.size() == 200);
  REQUIRE(b.none());
  b.set(0);
  b.set(63);
  b.set(64);
  b.set(199);
  REQUIRE(b.test(0));
  REQUIRE(b.test(63));
  REQUIRE(b.test(64));
  REQUIRE(b.test(199));
  REQUIRE_FALSE(b.test(1));
  REQUIRE(b.count() == 4);
  b.clearBit(64);
  REQUIRE_FALSE(b.test(64));
  REQUIRE(b.count() == 3);
}

TEST_CASE("BitWords uses a single word for a chessboard", "[unit][base]") {
  // The classical bitboard is the degenerate case of the general engine, not a
  // parallel code path (ARCH section 5).
  BitWords b(64);
  REQUIRE(b.wordCount() == 1);
}

TEST_CASE("BitWords forEach visits set bits in ascending order", "[unit][base]") {
  BitWords b(150);
  const std::vector<std::size_t> want{3, 17, 64, 65, 128, 149};
  for (std::size_t i : want) b.set(i);
  std::vector<std::size_t> got;
  b.forEach([&](std::size_t i) { got.push_back(i); });
  REQUIRE(got == want);
}

TEST_CASE("BitWords agrees with std::bitset over random patterns", "[unit][base]") {
  // Differential test against a reference implementation - the cheapest form of
  // the method in ADR-0009.
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 40)));
  cb::Rng rng(seed);
  constexpr std::size_t kN = 137;
  BitWords b(kN);
  std::bitset<kN> ref;
  for (int op = 0; op < 500; ++op) {
    const std::size_t i = rng.below(kN);
    if (rng.coin()) {
      b.set(i);
      ref.set(i);
    } else {
      b.clearBit(i);
      ref.reset(i);
    }
  }
  REQUIRE(b.count() == ref.count());
  for (std::size_t i = 0; i < kN; ++i) REQUIRE(b.test(i) == ref.test(i));
}
