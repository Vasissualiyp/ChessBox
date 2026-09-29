// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "movegen/movegen.hpp"
#include "position/position.hpp"

namespace cb::oracle {

/// A deliberately naive, obviously-correct move generator (ADR-0009).
///
/// It shares nothing with the real generator except the variant declaration and
/// the geometry's public step. In particular it:
///   - re-expands every atom's directions from scratch instead of using the
///     variant's precomputed direction table and spans,
///   - re-decodes the coordinate at every step instead of carrying it,
///   - iterates cells in a plain loop instead of walking occupancy bitsets,
///   - checks legality by scanning every enemy move instead of a reverse walk.
///
/// It is therefore slow, and that is the point: it is the reference every
/// optimisation is proven equal to. It is reviewed as carefully as production
/// code and is never allowed to rot.
std::vector<Move> pseudoLegal(const Position& p);
std::vector<Move> legal(Position& p);
bool isAttacked(const Position& p, CellId target, Color by);
std::uint64_t perft(Position& p, int depth);

/// Canonical sort so two generators' output can be compared as sequences.
void sortMoves(std::vector<Move>& moves);

}  // namespace cb::oracle
