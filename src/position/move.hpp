// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "position/piece.hpp"

namespace cb {

enum class MoveFlag : std::uint8_t {
  None = 0,
  Capture = 1 << 0,
  Promotion = 1 << 1,
  Castle = 1 << 2,
  EnPassant = 1 << 3,
  /// Left an en-passant target behind (a pawn's double step, generalized).
  LeavesEnPassant = 1 << 4,
};

inline std::uint8_t operator|(MoveFlag a, MoveFlag b) {
  return static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
inline bool has(std::uint8_t flags, MoveFlag f) {
  return (flags & static_cast<std::uint8_t>(f)) != 0;
}

/// One move. `captureCell` is separate from `to` because an en-passant capture
/// removes a piece from a cell the mover never occupies - and because a future
/// variant may capture at a distance.
struct Move {
  CellId from{kInvalidCell};
  CellId to{kInvalidCell};
  CellId captureCell{kInvalidCell};
  /// The cell an opponent may capture through next move, if this move leaves one.
  CellId epTarget{kInvalidCell};
  /// The piece that becomes vulnerable to that en-passant capture. Recorded
  /// rather than derived as "one cell behind the target", which is meaningless
  /// once the board wraps or has more axes.
  CellId epVictim{kInvalidCell};
  PieceTypeId promoteTo{kNoPiece};
  std::uint8_t flags{0};
  std::uint8_t castleIndex{0xFF};

  [[nodiscard]] bool isCapture() const noexcept { return has(flags, MoveFlag::Capture); }
  [[nodiscard]] bool isCastle() const noexcept { return has(flags, MoveFlag::Castle); }

  friend bool operator==(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.captureCell == b.captureCell &&
           a.promoteTo == b.promoteTo && a.flags == b.flags && a.castleIndex == b.castleIndex;
  }
};

static_assert(std::is_trivially_copyable_v<Move>);

/// Total order on moves, for canonical output and for deduplication.
inline bool moveLess(const Move& a, const Move& b) {
  if (a.from != b.from) return a.from < b.from;
  if (a.to != b.to) return a.to < b.to;
  if (a.captureCell != b.captureCell) return a.captureCell < b.captureCell;
  if (a.promoteTo != b.promoteTo) return a.promoteTo < b.promoteTo;
  if (a.flags != b.flags) return a.flags < b.flags;
  return a.castleIndex < b.castleIndex;
}

/// State that a move destroys and unmake must restore. Kept beside the move
/// rather than inside it so a Move stays a value that can be stored cheaply.
struct Undo {
  Piece captured{};
  CellId epBefore{kInvalidCell};
  CellId epVictimBefore{kInvalidCell};
  std::uint8_t castleRightsBefore{0};
  std::int32_t halfmoveBefore{0};
  std::uint64_t hashBefore{0};
};

}  // namespace cb
