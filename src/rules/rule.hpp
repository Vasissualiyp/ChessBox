// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rules/expr.hpp"

namespace cb::rules {

/// When a rule runs.
enum class Trigger : std::uint8_t {
  OnMoveFilter,   ///< before legality: may forbid a move (forced captures, bans)
  OnCapture,      ///< a piece was taken
  OnMoveEnd,      ///< after the move is on the board, before the turn passes
  OnTurnEnd,      ///< after the turn passes
  OnResultQuery,  ///< asked when the game result is computed
};

std::string_view toString(Trigger t) noexcept;

enum class EffectOp : std::uint8_t {
  Destroy,        ///< args[0] = cell
  DestroyRegion,  ///< args[0] = centre; imm = Chebyshev radius; `filter` gates each cell
  Transform,      ///< args[0] = cell, imm = new piece type
  Spawn,          ///< args[0] = cell, imm = piece type, args[1] = colour
  SetPieceField,  ///< imm = field index, args[0] = cell, args[1] = value
  SetCellField,   ///< imm = field index, args[0] = cell, args[1] = value
  ForbidMove,     ///< only meaningful under OnMoveFilter
  RepeatTurn,     ///< the mover moves again (multi-jump)
  EndGame,        ///< imm = GameOutcome
};

std::string_view toString(EffectOp op) noexcept;

/// What EndGame declares. Separate from the engine's GameResult so that rules stay a
/// layer below the game.
enum class Outcome : std::uint8_t { MoverWins, MoverLoses, Draw };

struct Effect {
  EffectOp op{EffectOp::ForbidMove};
  std::int64_t imm{0};
  std::vector<Expr> args;
  /// For iterating effects, a per-cell predicate with CellArg bound. Empty means "all".
  std::vector<Expr> filter;
};

struct Rule {
  std::string name;
  Trigger trigger{Trigger::OnMoveEnd};
  /// Empty means "always".
  std::vector<Expr> condition;
  std::vector<Effect> effects;
};

/// A variant's rules, in declaration order. Order is part of the variant's identity:
/// two rules that both fire must do so predictably.
struct RuleSet {
  std::vector<Rule> rules;
  [[nodiscard]] bool empty() const noexcept { return rules.empty(); }
  [[nodiscard]] std::string describe() const;
};

}  // namespace cb::rules
