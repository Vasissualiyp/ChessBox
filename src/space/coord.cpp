// SPDX-License-Identifier: GPL-3.0-or-later
#include "space/coord.hpp"

namespace cb {

std::string Coord::toString() const {
  std::string out = "(";
  for (std::uint8_t i = 0; i < n; ++i) {
    if (i != 0) out += ',';
    out += std::to_string(c[i]);
  }
  out += ')';
  return out;
}

Direction Direction::make(const std::array<std::int16_t, kMaxDims>& vec, std::uint8_t dims) {
  Direction d;
  d.n = dims;
  d.v = vec;
  for (std::uint8_t i = dims; i < kMaxDims; ++i) d.v[i] = 0;
  for (std::uint8_t i = 0; i < dims; ++i) {
    if (d.v[i] != 0) d.sup[d.nsup++] = i;
  }
  return d;
}

Direction Direction::negated() const {
  std::array<std::int16_t, kMaxDims> nv{};
  for (std::uint8_t i = 0; i < n; ++i) nv[i] = static_cast<std::int16_t>(-v[i]);
  return make(nv, n);
}

std::string Direction::toString() const {
  std::string out = "[";
  for (std::uint8_t i = 0; i < n; ++i) {
    if (i != 0) out += ',';
    out += std::to_string(v[i]);
  }
  out += ']';
  return out;
}

}  // namespace cb
