// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "position/position.hpp"
#include "variant/variant.hpp"

namespace cb::app {

/// Carry a position from one variant onto another's board (M13.9 part F). Every piece on
/// `from` is placed on `to` at the same coordinate on every axis the two variants share,
/// matched **by axis name** rather than by index - a variant that declares its axes in a
/// different order, or adds a new one in the middle, still lines up - and every axis only
/// `to` declares is fixed at 0, so the rest of that new axis starts empty ("embed on one
/// slice"). Piece types are matched by name too, for the same reason: a `PieceTypeId` is
/// only an index into its own variant's table, so a raw copy would turn one variant's
/// pawn into another's knight. A piece type `to` does not declare is omitted - see the
/// precondition below.
///
/// **Injective precondition.** Two source cells land on the same target cell only if a
/// shared axis's extent shrinks, so the mapping is one-to-one exactly when every shared
/// axis's extent in `to` is >= its extent in `from`. The attract sequence embeds only
/// upward (a 2-D board into an 8x8x4 one, then an 8x8x4 one into an 8x8x4x2 one), which
/// satisfies it; a caller embedding *downward* would silently collide pieces and must
/// not.
///
/// **Piece-set precondition.** For the embedding to be lossless, `to` must declare every
/// piece type present in `from`. The sequence's 4-D target (`hyper4_8x8`) declares the
/// full standard set so its beat carries everything; the Klein bottle declares no pawn
/// by design, so the pawns are dropped at that one cut - the alternative (authoring a
/// Klein bottle with pawns) would change a catalogue variant's identity. The side to move
/// carries forward unchanged.
[[nodiscard]] Position embedOnto(const Position& from, const VariantSpec& fromVariant,
                                 const VariantSpec& toVariant);

}  // namespace cb::app
