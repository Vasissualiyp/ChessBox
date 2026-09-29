// SPDX-License-Identifier: GPL-3.0-or-later
#include "geometry/transform.hpp"

namespace cb {

Transform Transform::identity(std::uint8_t dims) {
  Transform t;
  t.n = dims;
  for (std::uint8_t i = 0; i < dims; ++i) {
    t.src[i] = i;
    t.sign[i] = 1;
    t.off[i] = 0;
  }
  return t;
}

Coord Transform::applyToCoord(const Coord& p) const {
  Coord out(n);
  for (std::uint8_t i = 0; i < n; ++i) {
    out.c[i] = static_cast<std::int16_t>(sign[i] * p.c[src[i]] + off[i]);
  }
  return out;
}

Direction Transform::applyToDir(const Direction& d) const {
  std::array<std::int16_t, kMaxDims> v{};
  for (std::uint8_t i = 0; i < n; ++i) {
    v[i] = static_cast<std::int16_t>(sign[i] * d.v[src[i]]);
  }
  return Direction::make(v, n);
}

Transform Transform::compose(const Transform& other) const {
  // (this o other)[i] = sign[i] * other_out[src[i]] + off[i]
  //                   = sign[i] * (other.sign[src[i]] * x[other.src[src[i]]]
  //                                + other.off[src[i]]) + off[i]
  Transform r;
  r.n = n;
  for (std::uint8_t i = 0; i < n; ++i) {
    const std::uint8_t k = src[i];
    r.src[i] = other.src[k];
    r.sign[i] = static_cast<std::int8_t>(sign[i] * other.sign[k]);
    r.off[i] = static_cast<std::int16_t>(sign[i] * other.off[k] + off[i]);
  }
  return r;
}

Transform Transform::inverse() const {
  Transform r;
  r.n = n;
  // out[i] = sign[i]*in[src[i]] + off[i]  =>  in[src[i]] = sign[i]*(out[i]-off[i])
  for (std::uint8_t i = 0; i < n; ++i) {
    const std::uint8_t j = src[i];
    r.src[j] = i;
    r.sign[j] = sign[i];  // sign is its own inverse (+-1)
    r.off[j] = static_cast<std::int16_t>(-sign[i] * off[i]);
  }
  return r;
}

bool Transform::isIdentity() const {
  for (std::uint8_t i = 0; i < n; ++i) {
    if (src[i] != i || sign[i] != 1 || off[i] != 0) return false;
  }
  return true;
}

bool Transform::reversesOrientation() const {
  // det = sign(permutation) * product(sign[i])
  int negatives = 0;
  for (std::uint8_t i = 0; i < n; ++i) {
    if (sign[i] < 0) ++negatives;
  }
  // Parity of the permutation, counted by inversions.
  int inversions = 0;
  for (std::uint8_t i = 0; i < n; ++i) {
    for (std::uint8_t j = static_cast<std::uint8_t>(i + 1); j < n; ++j) {
      if (src[i] > src[j]) ++inversions;
    }
  }
  return ((negatives + inversions) % 2) != 0;
}

std::string Transform::toString() const {
  std::string out;
  for (std::uint8_t i = 0; i < n; ++i) {
    if (i != 0) out += ", ";
    out += 'a' + static_cast<char>(i);
    out += " <- ";
    if (sign[i] < 0) out += '-';
    out += 'a' + static_cast<char>(src[i]);
    if (off[i] > 0) out += "+" + std::to_string(off[i]);
    if (off[i] < 0) out += std::to_string(off[i]);
  }
  return out;
}

bool operator==(const Transform& a, const Transform& b) {
  if (a.n != b.n) return false;
  for (std::uint8_t i = 0; i < a.n; ++i) {
    if (a.src[i] != b.src[i] || a.sign[i] != b.sign[i] || a.off[i] != b.off[i])
      return false;
  }
  return true;
}

}  // namespace cb
