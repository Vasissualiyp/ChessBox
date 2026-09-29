// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "position/position.hpp"

namespace cb {

/// How to flatten an N-dimensional board onto a 2-D page.
///
/// Two axes are drawn as a grid; every remaining axis becomes an outer loop, so the
/// output is a sequence of labelled 2-D slices. This is the same projection model the
/// Vulkan renderer will use - screen axes plus a laid-out grid of sub-boards (ARCH
/// section 10) - which is why it lives here and is golden-tested in text rather than
/// being written twice.
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
