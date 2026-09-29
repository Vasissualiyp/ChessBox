// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cassert>
#include <vector>

#include "position/move.hpp"

namespace cb {

/// Move output buffer. Storage is reserved once, by its owner, from the variant's
/// `moveUpperBound()`; generation then only ever bumps a size. That is how the
/// "no allocation in movegen" invariant is met without a fixed capacity that
/// could silently overflow on an exotic board (AGENTS.md rule 6).
class MoveList {
 public:
  MoveList() = default;
  explicit MoveList(std::uint32_t capacity) { reserve(capacity); }

  void reserve(std::uint32_t capacity) { buf_.reserve(capacity); }

  void push(const Move& m) {
    assert(buf_.size() < buf_.capacity() &&
           "MoveList would reallocate inside movegen: raise VariantSpec::moveUpperBound");
    buf_.push_back(m);
  }

  void clear() noexcept { buf_.clear(); }
  [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
  [[nodiscard]] bool empty() const noexcept { return buf_.empty(); }
  [[nodiscard]] std::size_t capacity() const noexcept { return buf_.capacity(); }

  /// Put the list in canonical order and drop exact duplicates.
  ///
  /// Needed only on a board with glued faces: there, two directions of the same
  /// atom can reach one cell, and a ray can cross a cell twice, so the same move
  /// is discovered more than once. Deduplicating *here* rather than by cutting the
  /// ray short is what keeps reachability symmetric, which the reverse-attack
  /// search depends on.
  void sortAndUnique() {
    std::sort(buf_.begin(), buf_.end(), moveLess);
    buf_.erase(std::unique(buf_.begin(), buf_.end()), buf_.end());
  }

  const Move& operator[](std::size_t i) const { return buf_[i]; }
  Move& operator[](std::size_t i) { return buf_[i]; }
  [[nodiscard]] const Move* begin() const noexcept { return buf_.data(); }
  [[nodiscard]] const Move* end() const noexcept { return buf_.data() + buf_.size(); }

 private:
  std::vector<Move> buf_;
};

}  // namespace cb
