// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <string>

#include "space/dims.hpp"

namespace cb {

/// Human/authoring coordinates. Trivially copyable POD, no heap. Used ONLY by
/// IO, UI and tests - movegen never decodes a coordinate (ARCH section 2).
struct Coord {
  std::array<std::int16_t, kMaxDims> c{};
  std::uint8_t n{0};

  constexpr Coord() = default;
  constexpr explicit Coord(std::uint8_t dims) : n(dims) {}

  static constexpr Coord of(std::initializer_list<int> vals) {
    Coord out;
    for (int v : vals) {
      assert(out.n < kMaxDims);
      out.c[out.n++] = static_cast<std::int16_t>(v);
    }
    return out;
  }

  constexpr std::int16_t& operator[](std::size_t i) {
    assert(i < n);
    return c[i];
  }
  constexpr std::int16_t operator[](std::size_t i) const {
    assert(i < n);
    return c[i];
  }
  [[nodiscard]] constexpr std::uint8_t dims() const noexcept { return n; }

  friend constexpr bool operator==(const Coord& a, const Coord& b) {
    if (a.n != b.n) return false;
    for (std::uint8_t i = 0; i < a.n; ++i) {
      if (a.c[i] != b.c[i]) return false;
    }
    return true;
  }

  /// Generic textual form, e.g. "(3,5,2)". Chess-style names live in L9.
  [[nodiscard]] std::string toString() const;
};

static_assert(sizeof(Coord) <= 20, "Coord must stay small and POD");
static_assert(std::is_trivially_copyable_v<Coord>);

/// A signed direction vector in lattice space, with its support (the axes on
/// which it is nonzero) precomputed - the ray walk only ever touches those
/// (ARCH section 4.2).
struct Direction {
  std::array<std::int16_t, kMaxDims> v{};
  std::array<std::uint8_t, kMaxDims> sup{};  // supporting axis indices
  std::uint8_t n{0};                         // dimension count
  std::uint8_t nsup{0};                      // size of the support

  static Direction make(const std::array<std::int16_t, kMaxDims>& vec, std::uint8_t dims);

  [[nodiscard]] Direction negated() const;
  [[nodiscard]] std::string toString() const;

  friend constexpr bool operator==(const Direction& a, const Direction& b) {
    if (a.n != b.n) return false;
    for (std::uint8_t i = 0; i < a.n; ++i) {
      if (a.v[i] != b.v[i]) return false;
    }
    return true;
  }
};

static_assert(std::is_trivially_copyable_v<Direction>);

}  // namespace cb
