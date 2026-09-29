// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "movegen/movegen.hpp"
#include "rules/vm.hpp"
#include "temporal/multiverse.hpp"

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

  [[nodiscard]] std::size_t plyCount() const noexcept { return played_.size(); }
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

  /// True when the variant declares a temporal or multiverse axis, in which case play
  /// goes through the multiverse: a move appends a board, and time travel branches.
  [[nodiscard]] bool isTemporal() const noexcept { return temporal_; }
  /// The multiverse's board bookkeeping, for a temporal variant (nullptr otherwise).
  [[nodiscard]] const temporal::Multiverse* multiverse() const noexcept {
    return multi_.has_value() ? &*multi_ : nullptr;
  }
  /// For a temporal variant, whether this cell's board exists yet. The rest of the
  /// lattice is space the multiverse will fill; the view hides it until then.
  [[nodiscard]] bool boardVisible(CellId c) const;
  /// Whether this cell is on a board in the present column - the boards to answer on.
  [[nodiscard]] bool cellInPresent(CellId c) const;

 private:
  void invalidate();
  [[nodiscard]] int repetitionCount() const;

  // ---- temporal (M6) ------------------------------------------------------
  void initTemporal();
  [[nodiscard]] temporal::BoardKey keyOf(CellId c) const;
  [[nodiscard]] CellId cellOn(const temporal::BoardKey& b, std::int16_t file,
                              std::int16_t rank) const;
  [[nodiscard]] std::vector<temporal::BoardKey> actionable() const;
  void copyBoard(const temporal::BoardKey& from, const temporal::BoardKey& to);
  void applyTemporal(const Move& m);
  void settleTemporalTurn();

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

  // ---- temporal state -----------------------------------------------------
  bool temporal_{false};
  std::uint8_t fileAxis_{0};
  std::uint8_t rankAxis_{1};
  std::uint8_t turnAxis_{0};
  std::uint8_t lineAxis_{0};
  std::optional<temporal::Multiverse> multi_;
  Color temporalTurn_{Color::White};
  /// A whole-multiverse snapshot per move, because a temporal move rewrites many boards
  /// and is not reversible by a single Undo.
  struct TemporalSnapshot {
    Position pos;
    temporal::Multiverse multi;
    Color turn;
  };
  std::vector<TemporalSnapshot> temporalHistory_;
};

}  // namespace cb
