// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "movegen/movegen.hpp"
#include "rules/vm.hpp"

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
  RuleDeclared,  ///< a variant rule ended the game
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

  /// The variant's rule engine. Empty for variants that declare no rules, in which case
  /// nothing in the play path touches it at all.
  [[nodiscard]] const rules::RuleEngine& ruleEngine() const noexcept { return engine_; }

  /// Whether the last move left the same side to move again, as a multi-jump does.
  [[nodiscard]] bool turnRepeated() const noexcept {
    return !repeated_.empty() && repeated_.back();
  }

  /// A one-line human-readable status, used by both front ends.
  [[nodiscard]] std::string statusLine() const;

 private:
  void invalidate();
  [[nodiscard]] int repetitionCount() const;

  /// Does this piece have a capture available? Supplied to the rule VM, which cannot
  /// generate moves itself (see rules::RuleEnv).
  [[nodiscard]] bool hasCaptureFrom(CellId c) const;
  [[nodiscard]] rules::RuleEnv makeEnv(const Move* m, Color mover) const;

  const VariantSpec* v_;
  Position pos_;
  MoveGen gen_;
  rules::RuleEngine engine_;
  /// Per ply: did a rule keep the turn with the mover?
  std::vector<bool> repeated_;
  bool startHasRoyal_{false};
  bool ruleEnded_{false};
  rules::Outcome ruleOutcome_{rules::Outcome::Draw};
  Color ruleOutcomeMover_{Color::White};
  std::vector<Undo> history_;
  std::vector<Move> played_;
  /// Position hashes after every ply, including the starting position, for repetition.
  std::vector<std::uint64_t> hashes_;

  mutable std::vector<Move> legalCache_;
  mutable bool legalCacheValid_{false};
};

}  // namespace cb
