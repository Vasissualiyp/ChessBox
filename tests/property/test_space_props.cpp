// SPDX-License-Identifier: GPL-3.0-or-later
// Property tests over randomly shaped lattices. Hand-written cases cover a
// vanishing fraction of (dimension count x extents); invariants cover all of it
// (ADR-0009).
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "base/rng.hpp"
#include "space/dim_spec.hpp"

using namespace cb;

namespace {

/// A random small lattice: 1..kMaxDims axes with extents in 1..6, kept under the
/// cell budget so creation always succeeds.
DimSpec randomDimSpec(Rng& rng) {
  for (;;) {
    const int n = 1 + static_cast<int>(rng.below(kMaxDims));
    std::vector<AxisDecl> axes;
    std::uint64_t cells = 1;
    for (int i = 0; i < n; ++i) {
      const int e = 1 + static_cast<int>(rng.below(6));
      cells *= static_cast<std::uint64_t>(e);
      axes.push_back(AxisDecl{e, AxisKind::Spatial, "a" + std::to_string(i)});
    }
    if (cells > 200000) continue;
    auto d = DimSpec::create(axes);
    if (d.has_value()) return std::move(*d);
  }
}

}  // namespace

TEST_CASE("cell <-> coordinate is a bijection on every lattice", "[property][space]") {
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 60)));
  Rng rng(seed);
  const DimSpec d = randomDimSpec(rng);

  // Every cell decodes to an in-range coordinate that re-encodes to itself, and
  // the map is injective - so no two cells can ever alias.
  std::vector<bool> seen(d.cellCount(), false);
  for (CellId c = 0; c < d.cellCount(); ++c) {
    const Coord p = d.toCoord(c);
    REQUIRE(d.inRange(p));
    const CellId back = d.toCell(p);
    REQUIRE(back == c);
    REQUIRE_FALSE(seen[c]);
    seen[c] = true;
  }
}

TEST_CASE("delta equals the difference of encoded cells for interior steps",
          "[property][space]") {
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 60)));
  Rng rng(seed);
  const DimSpec d = randomDimSpec(rng);

  for (int trial = 0; trial < 200; ++trial) {
    // A random direction with components in -2..2.
    std::array<std::int16_t, kMaxDims> v{};
    for (std::uint8_t a = 0; a < d.dims(); ++a) {
      v[a] = static_cast<std::int16_t>(static_cast<int>(rng.below(5)) - 2);
    }
    const Direction dir = Direction::make(v, d.dims());

    const CellId from = rng.below(d.cellCount());
    const Coord p = d.toCoord(from);
    Coord q = p;
    bool interior = true;
    for (std::uint8_t a = 0; a < d.dims(); ++a) {
      const int nv = q.c[a] + dir.v[a];
      if (nv < 0 || nv >= d.extent(a)) {
        interior = false;
        break;
      }
      q.c[a] = static_cast<std::int16_t>(nv);
    }
    if (!interior) continue;

    REQUIRE(static_cast<std::int64_t>(from) + d.delta(dir) ==
            static_cast<std::int64_t>(d.toCell(q)));
  }
}

TEST_CASE("a lifted lattice preserves cell count and coordinates", "[property][space]") {
  // Dimension-lift invariance at the lattice level: adding an axis of extent 1
  // must change nothing observable. The full game-level version of this is the
  // backbone of M2.
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 40)));
  Rng rng(seed);
  const DimSpec base = randomDimSpec(rng);
  if (base.dims() >= kMaxDims) return;

  std::vector<AxisDecl> axes;
  for (std::uint8_t a = 0; a < base.dims(); ++a) {
    axes.push_back(AxisDecl{base.extent(a), base.kind(a), base.name(a)});
  }
  axes.push_back(AxisDecl{1, AxisKind::Spatial, "lifted"});
  const DimSpec lifted = DimSpec::create(axes).value();

  REQUIRE(lifted.cellCount() == base.cellCount());
  for (CellId c = 0; c < base.cellCount(); ++c) {
    const Coord p = base.toCoord(c);
    const Coord q = lifted.toCoord(c);
    for (std::uint8_t a = 0; a < base.dims(); ++a) REQUIRE(p.c[a] == q.c[a]);
    REQUIRE(q.c[base.dims()] == 0);
  }
}
