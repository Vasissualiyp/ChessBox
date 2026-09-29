// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "base/result.hpp"
#include "position/move.hpp"
#include "variant/variant.hpp"

namespace cb {

/// Name of a cell. For an ordinary 2-D board with at most 26 files and 9 ranks
/// this is algebraic notation ("e4"), so standard chess reads and writes exactly
/// as everyone expects; anything else gets the generic tuple form ("(3,5,2)").
std::string cellName(const DimSpec& d, CellId c);

/// Inverse of cellName, accepting either form.
Result<CellId> parseCell(const DimSpec& d, std::string_view text);

/// Long-algebraic-style move text: "e2e4", "e7e8q", "e1g1" for castling,
/// generalized to "(0,1)-(0,3)" on boards without algebraic names.
std::string moveText(const VariantSpec& v, const Move& m);

}  // namespace cb
