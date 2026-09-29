// SPDX-License-Identifier: GPL-3.0-or-later
#include "rules/rule.hpp"

namespace cb::rules {

std::string_view toString(Trigger t) noexcept {
  switch (t) {
    case Trigger::OnMoveFilter:
      return "on_move_filter";
    case Trigger::OnCapture:
      return "on_capture";
    case Trigger::OnMoveEnd:
      return "on_move_end";
    case Trigger::OnTurnEnd:
      return "on_turn_end";
    case Trigger::OnResultQuery:
      return "on_result_query";
  }
  return "?";
}

std::string_view toString(EffectOp op) noexcept {
  switch (op) {
    case EffectOp::Destroy:
      return "destroy";
    case EffectOp::DestroyRegion:
      return "destroy_region";
    case EffectOp::Transform:
      return "transform";
    case EffectOp::Spawn:
      return "spawn";
    case EffectOp::SetPieceField:
      return "set_piece_field";
    case EffectOp::SetCellField:
      return "set_cell_field";
    case EffectOp::ForbidMove:
      return "forbid_move";
    case EffectOp::RepeatTurn:
      return "repeat_turn";
    case EffectOp::EndGame:
      return "end_game";
  }
  return "?";
}

std::string RuleSet::describe() const {
  std::string s;
  for (const Rule& r : rules) {
    s += r.name.empty() ? "(unnamed)" : r.name;
    s += " [";
    s += toString(r.trigger);
    s += "]";
    if (!r.condition.empty()) s += " if " + r.condition[0].toString();
    s += " ->";
    for (const Effect& e : r.effects) {
      s += ' ';
      s += toString(e.op);
      if (e.imm != 0) s += "[" + std::to_string(e.imm) + "]";
    }
    s += '\n';
  }
  return s;
}

}  // namespace cb::rules
