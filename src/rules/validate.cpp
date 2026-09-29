// SPDX-License-Identifier: GPL-3.0-or-later
// Load-time checking of a rule set.
//
// The audience for these messages is a variant author who is not a programmer, so every
// one of them names the rule and says what is wrong in the vocabulary of the TOML file
// rather than of the interpreter.
#include "rules/vm.hpp"

namespace cb::rules {
namespace {

Result<void> checkExpr(const Expr& e, const VariantSpec& v, const std::string& where,
                       bool cellArgBound) {
  const int want = arity(e.op);
  if (want >= 0 && static_cast<int>(e.args.size()) != want) {
    return fail(ErrorCode::ValidationError, where + ": '" + std::string(toString(e.op)) +
                                                "' takes " + std::to_string(want) +
                                                " argument(s), not " +
                                                std::to_string(e.args.size()));
  }
  if (want < 0 && e.args.empty()) {
    return fail(ErrorCode::ValidationError, where + ": '" + std::string(toString(e.op)) +
                                                "' needs at least one argument");
  }
  if (e.op == ExprOp::CellArg && !cellArgBound) {
    return fail(ErrorCode::ValidationError,
                where +
                    ": 'cell' can only be used inside an effect that walks cells, "
                    "such as the filter of destroy_region");
  }
  if (e.op == ExprOp::PieceField &&
      (e.imm < 0 || e.imm >= static_cast<std::int64_t>(v.pieceFields.size()))) {
    return fail(ErrorCode::ValidationError, where + ": unknown piece field");
  }
  if (e.op == ExprOp::CellField &&
      (e.imm < 0 || e.imm >= static_cast<std::int64_t>(v.cellFields.size()))) {
    return fail(ErrorCode::ValidationError, where + ": unknown cell field");
  }
  if (e.op == ExprOp::CoordOf && (e.imm < 0 || e.imm >= v.dims.dims())) {
    return fail(ErrorCode::ValidationError,
                where + ": 'coord' names an axis that does not exist");
  }
  for (const Expr& a : e.args) {
    if (auto r = checkExpr(a, v, where, cellArgBound); !r.has_value()) return r;
  }
  return {};
}

}  // namespace

Result<void> RuleEngine::validate(const VariantSpec& v) const {
  /// A rule that could visit a whole large board per move would make every game
  /// unplayable; catching it at load is far kinder than at move 30.
  constexpr std::size_t kMaxExprNodes = 256;

  for (const Rule& r : rules_.rules) {
    const std::string where = "rule '" + (r.name.empty() ? "(unnamed)" : r.name) + "'";
    if (r.effects.empty()) {
      return fail(ErrorCode::ValidationError,
                  where + " has no effects, so it does nothing");
    }
    for (const Expr& c : r.condition) {
      if (c.nodeCount() > kMaxExprNodes) {
        return fail(ErrorCode::BudgetExceeded, where + ": condition is too large");
      }
      if (auto ok = checkExpr(c, v, where, false); !ok.has_value()) return ok;
    }

    for (const Effect& e : r.effects) {
      const std::string ewhere = where + ", effect '" + std::string(toString(e.op)) + "'";
      for (const Expr& a : e.args) {
        if (auto ok = checkExpr(a, v, ewhere, false); !ok.has_value()) return ok;
      }
      for (const Expr& f : e.filter) {
        if (auto ok = checkExpr(f, v, ewhere + " filter", true); !ok.has_value())
          return ok;
      }

      switch (e.op) {
        case EffectOp::ForbidMove:
          if (r.trigger != Trigger::OnMoveFilter) {
            return fail(ErrorCode::ValidationError,
                        ewhere +
                            " only means anything under on_move_filter; by any later "
                            "trigger the move has already been played");
          }
          break;
        case EffectOp::RepeatTurn:
          if (r.trigger == Trigger::OnMoveFilter) {
            return fail(ErrorCode::ValidationError,
                        ewhere + " cannot run before the move it repeats");
          }
          break;
        case EffectOp::Destroy:
        case EffectOp::Transform:
        case EffectOp::SetPieceField:
        case EffectOp::SetCellField:
        case EffectOp::Spawn:
          if (e.args.empty()) {
            return fail(ErrorCode::ValidationError, ewhere + " needs a target cell");
          }
          break;
        case EffectOp::DestroyRegion:
          if (e.args.empty()) {
            return fail(ErrorCode::ValidationError, ewhere + " needs a centre cell");
          }
          if (e.imm < 0 || e.imm > 4) {
            return fail(ErrorCode::ValidationError,
                        ewhere +
                            ": radius must be between 0 and 4 (a radius of r visits "
                            "(2r+1)^dims cells, which grows very fast)");
          }
          break;
        case EffectOp::EndGame:
          break;
      }

      if (e.op == EffectOp::Transform || e.op == EffectOp::Spawn) {
        if (e.imm <= 0 || e.imm >= static_cast<std::int64_t>(v.pieces.size())) {
          return fail(ErrorCode::ValidationError,
                      ewhere + " names an unknown piece type");
        }
      }
      if (e.op == EffectOp::SetPieceField &&
          (e.imm < 0 || e.imm >= static_cast<std::int64_t>(v.pieceFields.size()))) {
        return fail(ErrorCode::ValidationError, ewhere + " names an unknown piece field");
      }
      if (e.op == EffectOp::SetCellField &&
          (e.imm < 0 || e.imm >= static_cast<std::int64_t>(v.cellFields.size()))) {
        return fail(ErrorCode::ValidationError, ewhere + " names an unknown cell field");
      }
    }
  }
  return {};
}

}  // namespace cb::rules
