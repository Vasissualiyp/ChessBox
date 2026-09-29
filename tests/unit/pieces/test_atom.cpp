// SPDX-License-Identifier: GPL-3.0-or-later
#include "pieces/atom.hpp"

#include <set>

#include <catch2/catch_test_macros.hpp>

using namespace cb;

namespace {

SmallVec<std::int16_t, kMaxDims> mags(std::initializer_list<int> v) {
  SmallVec<std::int16_t, kMaxDims> out;
  for (int x : v) out.push(static_cast<std::int16_t>(x));
  return out;
}

std::size_t countDirs(std::initializer_list<int> m, int dims) {
  return expandAtom(mags(m), static_cast<std::uint8_t>(dims)).size();
}

}  // namespace

TEST_CASE("atom expansion matches hand-computed direction counts", "[unit][pieces]") {
  // The table from docs/plan/M1-core-2d.md section M1.4. These are the numbers a
  // reader can check by hand, which is exactly why they are pinned here.
  //                      D=1  D=2  D=3  D=4
  // [1]      rook step     2    4    6    8
  // [1,1]    bishop step   0    4   12   24
  // [1,2]    knight        0    8   24   48
  // [1,1,1]  trigonal      0    0    8   32
  // [1,2,3]  exotic        0    0   48  192
  REQUIRE(countDirs({1}, 1) == 2);
  REQUIRE(countDirs({1}, 2) == 4);
  REQUIRE(countDirs({1}, 3) == 6);
  REQUIRE(countDirs({1}, 4) == 8);

  REQUIRE(countDirs({1, 1}, 1) == 0);
  REQUIRE(countDirs({1, 1}, 2) == 4);
  REQUIRE(countDirs({1, 1}, 3) == 12);
  REQUIRE(countDirs({1, 1}, 4) == 24);

  REQUIRE(countDirs({1, 2}, 2) == 8);
  REQUIRE(countDirs({1, 2}, 3) == 24);
  REQUIRE(countDirs({1, 2}, 4) == 48);

  REQUIRE(countDirs({1, 1, 1}, 2) == 0);
  REQUIRE(countDirs({1, 1, 1}, 3) == 8);
  REQUIRE(countDirs({1, 1, 1}, 4) == 32);

  REQUIRE(countDirs({1, 2, 3}, 3) == 48);
  REQUIRE(countDirs({1, 2, 3}, 4) == 192);
}

TEST_CASE("atom expansion matches the closed form for every atom and dimension",
          "[unit][pieces]") {
  // Enumeration and formula are computed independently; agreement over the whole
  // small-atom space is what catches duplicate-magnitude double counting.
  const std::vector<std::vector<int>> atoms{
      {1}, {2}, {3}, {1, 1}, {1, 2}, {2, 2}, {1, 3}, {1, 1, 1}, {1, 1, 2},
      {1, 2, 2}, {1, 2, 3}, {1, 1, 1, 1}, {1, 1, 2, 2}, {1, 2, 3, 4}};
  for (const auto& a : atoms) {
    for (int d = 1; d <= kMaxDims; ++d) {
      const auto m = mags({});
      SmallVec<std::int16_t, kMaxDims> mm;
      for (int x : a) mm.push(static_cast<std::int16_t>(x));
      const auto dims = static_cast<std::uint8_t>(d);
      CAPTURE(a, d);
      REQUIRE(static_cast<std::uint64_t>(expandAtom(mm, dims).size()) ==
              expectedDirectionCount(mm, dims));
      (void)m;
    }
  }
}

TEST_CASE("an atom needing more axes than the board has expands to nothing",
          "[unit][pieces]") {
  // A deliberate decision, not an error: a 3-D-only piece on a 2-D board simply
  // has no moves of that kind (M1.4).
  REQUIRE(expandAtom(mags({1, 1, 1}), 2).empty());
  REQUIRE(expandAtom(mags({1, 2, 3, 4}), 3).empty());
}

TEST_CASE("expanded directions are distinct and correctly shaped", "[unit][pieces]") {
  const auto dirs = expandAtom(mags({1, 2}), 3);
  std::set<std::string> seen;
  for (const Direction& d : dirs) {
    REQUIRE(seen.insert(d.toString()).second);  // no duplicates
    REQUIRE(d.nsup == 2);                       // exactly two nonzero components
    std::vector<int> abs;
    for (std::uint8_t i = 0; i < d.n; ++i) {
      if (d.v[i] != 0) abs.push_back(std::abs(d.v[i]));
    }
    std::sort(abs.begin(), abs.end());
    REQUIRE(abs == std::vector<int>{1, 2});
  }
  REQUIRE(dirs.size() == 24);
}

TEST_CASE("expansion is deterministic and canonically ordered", "[unit][pieces]") {
  // Direction indices are baked into the variant's tables and into hashes, so the
  // order must not depend on the compiler or the run.
  const auto a = expandAtom(mags({1, 2}), 4);
  const auto b = expandAtom(mags({2, 1}), 4);  // canonicalization makes these equal
  REQUIRE(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i) REQUIRE(a[i] == b[i]);
  for (std::size_t i = 1; i < a.size(); ++i) {
    REQUIRE(std::lexicographical_compare(a[i - 1].v.begin(), a[i - 1].v.begin() + 4,
                                         a[i].v.begin(), a[i].v.begin() + 4));
  }
}

TEST_CASE("the direction count grows as the spec's worst case predicts",
          "[unit][pieces]") {
  // [1,2,3] at 8 dimensions is P(8,3) * 2^3 = 336 * 8 = 2688 directions. The
  // load-time budget exists because of numbers like this (M2.3).
  REQUIRE(expectedDirectionCount(mags({1, 2, 3}), 8) == 2688);
  REQUIRE(expandAtom(mags({1, 2, 3}), 8).size() == 2688);
}

TEST_CASE("atom canonicalization sorts magnitudes and rejects nonsense",
          "[unit][pieces]") {
  MoveAtom a;
  a.mags = mags({3, 1, 2});
  const auto c = MoveAtom::canonicalize(a);
  REQUIRE(c.has_value());
  REQUIRE(c->mags[0] == 1);
  REQUIRE(c->mags[2] == 3);

  SECTION("no magnitudes") {
    MoveAtom bad;
    REQUIRE_FALSE(MoveAtom::canonicalize(bad).has_value());
  }
  SECTION("a zero magnitude is written by omission, not by a zero") {
    MoveAtom bad;
    bad.mags = mags({1, 0});
    const auto r = MoveAtom::canonicalize(bad);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().message.find("positive") != std::string::npos);
  }
  SECTION("maxK of zero") {
    MoveAtom bad;
    bad.mags = mags({1});
    bad.maxK = 0;
    REQUIRE_FALSE(MoveAtom::canonicalize(bad).has_value());
  }
}

TEST_CASE("atoms compare equal after canonicalization", "[unit][pieces]") {
  MoveAtom a;
  a.mags = mags({2, 1});
  MoveAtom b;
  b.mags = mags({1, 2});
  REQUIRE(MoveAtom::canonicalize(a).value() == MoveAtom::canonicalize(b).value());
}

TEST_CASE("oriented expansion gives a pawn its direction in any dimension",
          "[unit][pieces]") {
  SECTION("2-D push") {
    const auto white = expandAtomOriented(mags({1}), 2, 1, Color::White);
    REQUIRE(white.size() == 1);
    REQUIRE(white[0].v[1] == 1);
    const auto black = expandAtomOriented(mags({1}), 2, 1, Color::Black);
    REQUIRE(black.size() == 1);
    REQUIRE(black[0].v[1] == -1);
  }
  SECTION("2-D capture") {
    const auto white = expandAtomOriented(mags({1, 1}), 2, 1, Color::White);
    REQUIRE(white.size() == 2);
    for (const Direction& d : white) REQUIRE(d.v[1] == 1);
  }
  SECTION("3-D capture fans out sideways but never backwards") {
    const auto white = expandAtomOriented(mags({1, 1}), 3, 1, Color::White);
    REQUIRE(white.size() == 4);  // (+-1, +1, 0) and (0, +1, +-1)
    for (const Direction& d : white) REQUIRE(d.v[1] == 1);
  }
  SECTION("White and Black expansions are exact negations") {
    const auto w = expandAtomOriented(mags({1, 2}), 4, 0, Color::White);
    const auto b = expandAtomOriented(mags({1, 2}), 4, 0, Color::Black);
    REQUIRE(w.size() == b.size());
    std::set<std::string> negated;
    for (const Direction& d : w) negated.insert(d.negated().toString());
    for (const Direction& d : b) REQUIRE(negated.contains(d.toString()));
  }
}
