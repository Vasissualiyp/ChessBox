// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include "base/result.hpp"
#include "space/coord.hpp"

namespace cb {

/// What a lattice axis means. Metadata only below L7: move generation and
/// geometry treat every axis identically, which is precisely what makes time
/// travel and extra multiverse dimensions configuration rather than new code
/// (ADR-0007).
enum class AxisKind : std::uint8_t { Spatial, Temporal, Multiverse };

std::string_view toString(AxisKind k) noexcept;

struct AxisDecl {
  std::int32_t extent{0};
  AxisKind kind{AxisKind::Spatial};
  std::string name;
  /// How many lattice cells one *unit of movement* along this axis covers.
  ///
  /// Almost always 1: a rook moving one square along a file moves one cell. The turn
  /// axis is the exception that forced it to exist. Every half-move appends a board, so
  /// boards along that axis alternate whose move it is, and one turn of travel is two
  /// boards - a piece stepping a single board through time would arrive on a board
  /// where it is the opponent to move, which is not a board it may be on at all.
  ///
  /// It is a property of the *axis*, not of the piece: every piece that can move through
  /// time does so in whole turns, so stating it once beside the axis is both shorter and
  /// impossible to declare inconsistently.
  std::int32_t pitch{1};
};

/// The shape of a lattice: extents, kinds, and the row-major strides that turn a
/// coordinate into a flat CellId.
class DimSpec {
 public:
  static Result<DimSpec> create(std::span<const AxisDecl> axes);

  [[nodiscard]] std::uint8_t dims() const noexcept { return n_; }
  [[nodiscard]] std::int16_t extent(std::size_t a) const noexcept { return extent_[a]; }
  [[nodiscard]] AxisKind kind(std::size_t a) const noexcept { return kind_[a]; }
  [[nodiscard]] std::int16_t pitch(std::size_t a) const noexcept { return pitch_[a]; }
  [[nodiscard]] const std::string& name(std::size_t a) const noexcept {
    return names_[a];
  }
  [[nodiscard]] std::uint32_t stride(std::size_t a) const noexcept { return stride_[a]; }
  [[nodiscard]] std::uint32_t cellCount() const noexcept { return cellCount_; }

  /// Index of the named axis, or -1.
  [[nodiscard]] int axisIndex(std::string_view name) const;

  [[nodiscard]] bool inRange(const Coord& p) const noexcept {
    if (p.n != n_) return false;
    for (std::uint8_t i = 0; i < n_; ++i) {
      if (p.c[i] < 0 || p.c[i] >= extent_[i]) return false;
    }
    return true;
  }

  [[nodiscard]] CellId toCell(const Coord& p) const noexcept {
    std::uint32_t idx = 0;
    for (std::uint8_t i = 0; i < n_; ++i) {
      idx += static_cast<std::uint32_t>(p.c[i]) * stride_[i];
    }
    return idx;
  }

  [[nodiscard]] Coord toCoord(CellId cell) const noexcept {
    Coord p(n_);
    for (std::uint8_t i = n_; i-- > 0;) {
      p.c[i] = static_cast<std::int16_t>(cell / stride_[i]);
      cell %= stride_[i];
    }
    return p;
  }

  /// Flat delta for a direction, valid only for steps that stay in the interior.
  [[nodiscard]] std::int32_t delta(const Direction& d) const noexcept {
    std::int32_t s = 0;
    for (std::uint8_t k = 0; k < d.nsup; ++k) {
      const std::uint8_t a = d.sup[k];
      s += static_cast<std::int32_t>(d.v[a]) * static_cast<std::int32_t>(stride_[a]);
    }
    return s;
  }

 private:
  std::uint8_t n_{0};
  std::array<std::int16_t, kMaxDims> extent_{};
  std::array<AxisKind, kMaxDims> kind_{};
  std::array<std::int16_t, kMaxDims> pitch_{};
  std::array<std::uint32_t, kMaxDims> stride_{};
  std::array<std::string, kMaxDims> names_{};
  std::uint32_t cellCount_{0};
};

}  // namespace cb
