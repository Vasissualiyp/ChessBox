// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "rules/rule.hpp"
#include "variant/variant.hpp"

namespace cb {

/// Parse a rule expression written as an S-expression.
///
/// S-expressions rather than infix: the grammar is a dozen lines, the errors can point
/// at the offending token, and there is no precedence for an author to get wrong. What
/// matters for a variant file is that a condition is unambiguous, not that it is pretty.
///
///   (ne (type_at cell) piece:pawn)
///   (and (is_capture) (eq (coord rank move.to) 7))
///   (gt (piece_field charge move.to) 2)
///
/// Names are resolved against the variant as it stands: `piece:queen` becomes a type
/// id, `charge` a field index, `rank` an axis index. A typo is therefore a load error
/// naming the thing that does not exist, not a silent zero.
Result<rules::Expr> parseRuleExpr(const VariantSpec& v, std::string_view text);

}  // namespace cb
