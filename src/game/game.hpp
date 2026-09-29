// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "movegen/movegen.hpp"

namespace cb {

enum class GameResult : std::uint8_t {
  InProgress,
  WhiteWins,
  BlackWins,
  Draw,
};

std::string_view toString(GameResult r) noexcept;

/// Why a game ended. Kept separate from the result because "draw" alone tells a player
/// nothing, and because the reasons are variant-configurable.
enum class EndReason : std::uint8_t {
  None,
  Checkmate,
  Stalemate,
  DrawClock,     ///< the variant's halfmove limit
  Repetition,    ///< the same position three times
  NoLegalMoves,  ///< no royal piece exists, so "checkmate" does not apply
};

std::string_view toString(EndReason r) noexcept;

/// A position plus its history: the thing a player actually interacts with.
///
/// Adjudication lives here rather than in movegen because it is entirely
/// variant-policy - stalemate is a draw in chess, a loss in some variants and a win in
/// others, and a variant with no royal piece has no notion of checkmate at all.
class Game {
 public:
  explicit Game(const VariantSpec& v);

  [[nodiscard]] const VariantSpec& variant() const noexcept { return *v_; }
  [[nodiscard]] const Position& position() const noexcept { return pos_; }
  [[nodiscard]] Position& position() noexcept { return pos_; }
  [[nodiscard]] const MoveGen& moveGen() const noexcept { return gen_; }

  /// Legal moves in the current position, recomputed on demand.
  [[nodiscard]] const std::vector<Move>& legalMoves() const;

  [[nodiscard]] bool isLegal(const Move& m) const;
  Result<void> play(const Move& m);
  bool undo();
  void reset();

  [[nodiscard]] std::size_t plyCount() const noexcept { return history_.size(); }
  [[nodiscard]] const std::vector<Move>& moveHistory() const noexcept { return played_; }

  [[nodiscard]] GameResult result() const;
  [[nodiscard]] EndReason endReason() const;
  [[nodiscard]] bool inCheck() const;

  /// A one-line human-readable status, used by both front ends.
  [[nodiscard]] std::string statusLine() const;

 private:
  void invalidate();
  [[nodiscard]] int repetitionCount() const;

  const VariantSpec* v_;
  Position pos_;
  MoveGen gen_;
  std::vector<Undo> history_;
  std::vector<Move> played_;
  /// Position hashes after every ply, including the starting position, for repetition.
  std::vector<std::uint64_t> hashes_;

  mutable std::vector<Move> legalCache_;
  mutable bool legalCacheValid_{false};
};

}  // namespace cb
