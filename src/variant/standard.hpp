// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "base/result.hpp"
#include "variant/variant.hpp"

namespace cb {

/// Standard chess, built programmatically from the general primitives.
///
/// It exists for two reasons. First, the layers below L9 need a real variant to
/// test against without depending on the TOML loader. Second, and more
/// importantly, it is the fidelity check on the loader itself: a golden test
/// asserts that loading variants/standard.toml produces an identical variantId,
/// so the data path and the code path cannot drift.
///
/// Nothing here is special-cased in the engine. A rook is [1]^inf slide; castling
/// is a displacement template; "forward" is an orientation axis. The published
/// perft counts reproducing from this definition is what proves the generalized
/// machinery correct (M1.6).
Result<VariantSpec> makeStandardChess();

/// Standard chess lifted into `dims` axes, with extent 1 on every added axis.
/// Used by the dimension-lift invariance property: the lifted game must be
/// move-for-move identical to the 2-D one (M2.2).
Result<VariantSpec> makeStandardChessLifted(int dims);

}  // namespace cb
