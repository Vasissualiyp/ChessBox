// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "space/coord.hpp"

namespace cb {

/// A signed-permutation affine map on lattice coordinates:
///
///     out[i] = sign[i] * in[src[i]] + off[i]
///
/// This family is exactly what boundary identifications need - wraps (offset),
/// reflections and orientation reversals (sign), and axis exchanges (src) - and
/// it is closed under composition, so the identifications generate a finite
/// transition group (ADR-0004).
///
/// The induced action on a DIRECTION vector is the linear part alone:
///
///     dir_out[i] = sign[i] * dir_in[src[i]]
///
/// Deriving it here rather than letting a variant author state it is what makes
/// it impossible to declare a geometry whose direction transport is wrong - the
/// single most dangerous class of silent error on non-orientable boards.
struct Transform {
  std::array<std::uint8_t, kMaxDims> src{};
  std::array<std::int8_t, kMaxDims> sign{};
  std::array<std::int16_t, kMaxDims> off{};
  std::uint8_t n{0};

  static Transform identity(std::uint8_t dims);

  [[nodiscard]] Coord applyToCoord(const Coord& p) const;
  [[nodiscard]] Direction applyToDir(const Direction& d) const;

  /// this after other: (this * other)(x) == this(other(x)).
  [[nodiscard]] Transform compose(const Transform& other) const;
  [[nodiscard]] Transform inverse() const;
  [[nodiscard]] bool isIdentity() const;

  /// True when the linear part has negative determinant, i.e. the map reverses
  /// orientation. Klein and Moebius seams do; wraps and axis swaps in pairs
  /// do not. Exposed because several rules (pawn direction, colour binding) need
  /// to know (M3.4).
  [[nodiscard]] bool reversesOrientation() const;

  [[nodiscard]] std::string toString() const;

  friend bool operator==(const Transform& a, const Transform& b);
};

}  // namespace cb
