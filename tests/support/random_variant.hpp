// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "base/rng.hpp"
#include "variant/variant.hpp"

namespace cb::test {

struct RandomVariantOptions {
  int minDims{2};
  int maxDims{4};
  int minExtent{3};
  int maxExtent{6};
  int maxPieceTypes{4};
  int maxAtomsPerPiece{2};
  bool allowGeometry{true};
  bool allowOriented{true};
  bool requireRoyal{true};
};

/// Generate a random *valid* variant from a seed.
///
/// This is the engine of the property-testing strategy: in a generalized engine
/// the bugs live in the interaction of (dimension count x extents x topology x
/// atom sets), and hand-written cases cover a vanishing fraction of it. Same seed
/// always yields the same variant, on every compiler and platform (ADR-0009).
VariantSpec randomVariant(std::uint64_t seed, const RandomVariantOptions& opts = {});

/// A random reachable-looking position for a variant: the declared start, then a
/// random legal playout of up to `plies` moves. Returns the position and how many
/// plies were actually played (a game can end early).
struct RandomPlayout {
  int pliesPlayed{0};
  bool endedEarly{false};
};

}  // namespace cb::test
