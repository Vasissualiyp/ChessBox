// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/random_variant.hpp"

#include <set>

namespace cb::test {
namespace {

MoveAtom randomAtom(Rng& rng, int dims, bool allowOriented) {
  MoveAtom a;
  const int order =
      1 + static_cast<int>(rng.below(static_cast<std::uint32_t>(std::min(dims, 3))));
  for (int i = 0; i < order; ++i) {
    a.mags.push(static_cast<std::int16_t>(1 + rng.below(2)));  // magnitudes 1..2
  }
  switch (rng.below(3)) {
    case 0:
      a.mode = MoveMode::Leap;
      a.maxK = 1;
      break;
    case 1:
      a.mode = MoveMode::Slide;
      a.maxK = 1 + rng.below(3);
      break;
    default:
      a.mode = MoveMode::Slide;
      a.maxK = kUnlimited;
      break;
  }
  switch (rng.below(6)) {
    case 0:
      a.capture = CapturePolicy::Cannot;
      break;
    case 1:
      a.capture = CapturePolicy::Must;
      break;
    default:
      a.capture = CapturePolicy::May;
      break;
  }
  if (allowOriented && rng.below(4) == 0) a.oriented = true;
  return a;
}

}  // namespace

VariantSpec randomVariant(std::uint64_t seed, const RandomVariantOptions& opts) {
  Rng rng(seed);

  for (int attempt = 0; attempt < 64; ++attempt) {
    VariantSpec v;
    v.name = "random-" + std::to_string(seed);

    const int dims = opts.minDims + static_cast<int>(rng.below(static_cast<std::uint32_t>(
                                        opts.maxDims - opts.minDims + 1)));
    std::vector<AxisDecl> axes;
    std::uint64_t cells = 1;
    for (int i = 0; i < dims; ++i) {
      const int extent =
          opts.minExtent + static_cast<int>(rng.below(static_cast<std::uint32_t>(
                               opts.maxExtent - opts.minExtent + 1)));
      cells *= static_cast<std::uint64_t>(extent);
      axes.push_back(AxisDecl{extent, AxisKind::Spatial, "a" + std::to_string(i)});
    }
    if (cells > 4096) continue;  // keep property tests fast
    auto d = DimSpec::create(axes);
    if (!d.has_value()) continue;
    v.dims = *d;
    v.orientationAxis = static_cast<int>(rng.below(static_cast<std::uint32_t>(dims)));

    std::vector<IdentDecl> idents;
    if (opts.allowGeometry) {
      for (int a = 0; a < dims; ++a) {
        const std::uint32_t roll = rng.below(6);
        if (roll == 0) {
          IdentDecl id;
          id.axis = static_cast<std::uint8_t>(a);
          id.kind = BoundaryKind::Periodic;
          idents.push_back(id);
        } else if (roll == 1 && dims >= 2) {
          // A seam that flips another axis: a Moebius/Klein-style gluing.
          IdentDecl id;
          id.axis = static_cast<std::uint8_t>(a);
          id.kind = BoundaryKind::Periodic;
          id.flipAxes.push_back(static_cast<std::uint8_t>((a + 1) % dims));
          idents.push_back(id);
        } else if (roll == 2) {
          IdentDecl id;
          id.axis = static_cast<std::uint8_t>(a);
          id.side = rng.coin() ? Side::Max : Side::Min;
          id.kind = BoundaryKind::Mirror;
          idents.push_back(id);
        }
      }
    }
    auto geom = Geometry::create(v.dims, idents);
    if (!geom.has_value()) continue;
    v.geom = *geom;

    v.pieces.clear();
    v.pieces.emplace_back();
    const int types =
        1 + static_cast<int>(rng.below(static_cast<std::uint32_t>(opts.maxPieceTypes)));
    for (int t = 0; t < types; ++t) {
      PieceTypeDef p;
      p.name = "p" + std::to_string(t);
      p.symbol = static_cast<char>('A' + t);
      const int atoms =
          1 +
          static_cast<int>(rng.below(static_cast<std::uint32_t>(opts.maxAtomsPerPiece)));
      for (int i = 0; i < atoms; ++i) {
        auto a = MoveAtom::canonicalize(randomAtom(rng, dims, opts.allowOriented));
        if (a.has_value()) p.atoms.push_back(*a);
      }
      if (p.atoms.empty())
        p.atoms.push_back(*MoveAtom::canonicalize(MoveAtom{.mags = {1}}));
      v.pieces.push_back(std::move(p));
    }
    if (opts.requireRoyal) v.pieces.back().royal = true;

    // Scatter pieces, leaving most of the board empty so rays actually run.
    std::set<CellId> used;
    const auto count =
        2 + rng.below(static_cast<std::uint32_t>(v.dims.cellCount() / 4 + 1));
    for (std::uint32_t i = 0; i < count; ++i) {
      const CellId c = rng.below(v.dims.cellCount());
      if (!used.insert(c).second) continue;
      const auto type =
          static_cast<PieceTypeId>(1 + rng.below(static_cast<std::uint32_t>(types)));
      v.start.push_back(
          StartPiece{v.dims.toCoord(c), type, rng.coin() ? Color::White : Color::Black});
    }
    if (opts.requireRoyal) {
      // Give each side exactly one royal, so the legality rule is exercised.
      const auto royalType = static_cast<PieceTypeId>(types);
      for (int ci = 0; ci < kNumColors; ++ci) {
        for (int tries = 0; tries < 200; ++tries) {
          const CellId c = rng.below(v.dims.cellCount());
          if (!used.insert(c).second) continue;
          v.start.push_back(
              StartPiece{v.dims.toCoord(c), royalType, static_cast<Color>(ci)});
          break;
        }
      }
    }
    if (v.start.empty()) continue;

    if (auto ok = v.finalize(); !ok.has_value()) continue;
    return v;
  }

  // Fall back to something trivially valid rather than looping forever.
  VariantSpec v;
  v.name = "random-fallback";
  v.dims = DimSpec::create(std::vector<AxisDecl>{{4, AxisKind::Spatial, "x"},
                                                 {4, AxisKind::Spatial, "y"}})
               .value();
  v.geom = Geometry::create(v.dims, {}).value();
  v.pieces.clear();
  v.pieces.emplace_back();
  PieceTypeDef k;
  k.name = "k";
  k.symbol = 'K';
  k.royal = true;
  k.atoms.push_back(*MoveAtom::canonicalize(MoveAtom{.mags = {1}}));
  v.pieces.push_back(k);
  v.start.push_back(StartPiece{Coord::of({0, 0}), 1, Color::White});
  v.start.push_back(StartPiece{Coord::of({3, 3}), 1, Color::Black});
  (void)v.finalize();
  return v;
}

}  // namespace cb::test
