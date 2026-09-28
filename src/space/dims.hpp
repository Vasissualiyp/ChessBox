// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace cb {

/// Compile-time dimension budget (ADR-0003). This constant lives here and
/// NOWHERE else - raising it is a one-line change plus a rebuild.
///
/// 8 covers the spec's stated worst case: a 5-D board on a torus with time
/// travel is 5 spatial + turn + timeline = 7 axes.
constexpr int kMaxDims = 8;

/// Flat index into the lattice. The only square representation the hot paths
/// touch (ARCH section 2).
using CellId = std::uint32_t;
constexpr CellId kInvalidCell = 0xFFFF'FFFFu;

/// Index into a variant's global direction table.
using DirId = std::uint16_t;
constexpr DirId kInvalidDir = 0xFFFFu;

/// Ceiling on lattice size, so a hostile or mistaken variant fails validation
/// instead of exhausting memory.
constexpr std::uint64_t kMaxCells = 1u << 26;  // 67M cells

}  // namespace cb
