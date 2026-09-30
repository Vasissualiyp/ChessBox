// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "base/result.hpp"
#include "movegen/movegen.hpp"
#include "position/move.hpp"
#include "position/position.hpp"
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

/// Short algebraic notation - "Nf3", "exd5", "O-O", with "+"/"#" - for a player.
///
/// Disambiguation is by file, then rank, then the full from-cell, so a board where "the
/// b-file" means nothing still produces a unique string. Falls back to `moveText` for a
/// move SAN cannot express (a rule displacement, or a castle on a variant with no castle
/// templates). The check suffix is left off for a temporal variant, whose move sets do
/// not have the ordinary "position after this move" meaning.
std::string sanText(const VariantSpec& v, const Position& pos, const Move& m,
                    const MoveList& legal);

}  // namespace cb
