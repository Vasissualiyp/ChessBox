// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "position/position.hpp"

namespace cb {

/// How to flatten an N-dimensional board onto a 2-D page.
///
/// Two axes are drawn as a grid; every remaining axis becomes a slice, exactly as in
/// `view::ViewConfig`. The slices themselves come from `view::enumerateSlices`, so the
/// text board and the Vulkan renderer share one definition of the projection (ARCH
/// section 10) - and it is golden-tested here, in text, where a layout regression is a
/// failing string comparison rather than a confusing picture.
struct AsciiView {
  std::uint8_t axisX{0};
  std::uint8_t axisY{1};
  bool labels{true};
  /// Character for an empty cell.
  char empty{'.'};
};

/// Render a position as text. Rows are drawn with the Y axis descending, matching
/// both FEN's rank order and how a chessboard is normally printed.
std::string renderBoard(const Position& p, const AsciiView& view = {});

}  // namespace cb
