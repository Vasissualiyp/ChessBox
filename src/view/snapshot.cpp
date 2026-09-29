// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/snapshot.hpp"

namespace cb::view {

PositionView PositionView::capture(const Position& p) {
  PositionView v;
  v.variant_ = &p.variant();
  v.cells_.resize(p.cellCount());
  for (CellId c = 0; c < p.cellCount(); ++c) v.cells_[c] = p.at(c);
  v.side_ = p.sideToMove();
  v.ep_ = p.epTarget();
  v.hash_ = p.hash();
  return v;
}

}  // namespace cb::view
