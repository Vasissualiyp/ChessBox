// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "position/position.hpp"

namespace cb::view {

/// An immutable copy of everything a renderer needs to draw a position.
///
/// The renderer never touches a live Position: the engine would then have to
/// synchronise with the GPU, and a half-applied move would be drawable. Instead the
/// engine hands over a snapshot, which is a flat copy of the cell array plus the
/// small amount of state that is not on the board (ARCH section 10). **[INVARIANT]**
///
/// The variant is held by pointer, not copied - it is immutable and must outlive both
/// the position and the snapshot anyway (see Position's lifetime note).
class PositionView {
 public:
  static PositionView capture(const Position& p);

  [[nodiscard]] const VariantSpec& variant() const noexcept { return *variant_; }
  [[nodiscard]] Piece at(CellId c) const noexcept { return cells_[c]; }
  [[nodiscard]] std::uint32_t cellCount() const noexcept {
    return static_cast<std::uint32_t>(cells_.size());
  }
  [[nodiscard]] std::span<const Piece> cells() const noexcept { return cells_; }
  [[nodiscard]] Color sideToMove() const noexcept { return side_; }
  [[nodiscard]] std::uint64_t hash() const noexcept { return hash_; }
  [[nodiscard]] CellId epTarget() const noexcept { return ep_; }

  /// Cells the UI is currently highlighting: a selected piece and its legal
  /// destinations. Part of the snapshot so the renderer has one source of truth for a
  /// frame and cannot disagree with the engine about what is legal.
  [[nodiscard]] std::span<const CellId> highlighted() const noexcept {
    return highlight_;
  }
  void setHighlighted(std::vector<CellId> cells) { highlight_ = std::move(cells); }
  [[nodiscard]] CellId selected() const noexcept { return selected_; }
  void setSelected(CellId c) noexcept { selected_ = c; }

 private:
  const VariantSpec* variant_{nullptr};
  std::vector<Piece> cells_;
  std::vector<CellId> highlight_;
  CellId selected_{kInvalidCell};
  Color side_{Color::White};
  CellId ep_{kInvalidCell};
  std::uint64_t hash_{0};
};

}  // namespace cb::view
