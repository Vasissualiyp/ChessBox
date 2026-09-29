// SPDX-License-Identifier: GPL-3.0-or-later
#include "pieces/atom.hpp"

#include <algorithm>
#include <numeric>
#include <set>

namespace cb {
namespace {

/// Recursively assign each magnitude to an unused axis, then fan out over signs.
void enumerate(const SmallVec<std::int16_t, kMaxDims>& mags, std::uint8_t dims,
               std::size_t idx, std::array<std::int16_t, kMaxDims>& vec,
               std::uint32_t usedAxes,
               std::set<std::array<std::int16_t, kMaxDims>>& out) {
  if (idx == mags.size()) {
    out.insert(vec);
    return;
  }
  for (std::uint8_t a = 0; a < dims; ++a) {
    const std::uint32_t bit = 1u << a;
    if ((usedAxes & bit) != 0) continue;
    for (const int sign : {1, -1}) {
      vec[a] = static_cast<std::int16_t>(sign * mags[idx]);
      enumerate(mags, dims, idx + 1, vec, usedAxes | bit, out);
    }
    vec[a] = 0;
  }
}

std::uint64_t factorial(std::uint64_t n) {
  std::uint64_t f = 1;
  for (std::uint64_t i = 2; i <= n; ++i) f *= i;
  return f;
}

}  // namespace

Result<MoveAtom> MoveAtom::canonicalize(MoveAtom a) {
  if (a.mags.empty()) {
    return fail(ErrorCode::ValidationError, "a move atom needs at least one magnitude");
  }
  for (std::size_t i = 0; i < a.mags.size(); ++i) {
    if (a.mags[i] <= 0) {
      return fail(ErrorCode::ValidationError,
                  "move atom magnitudes must be positive; got " +
                      std::to_string(a.mags[i]) +
                      " (a zero component is implied by omission, not written)");
    }
  }
  std::sort(a.mags.begin(), a.mags.end());
  if (a.maxK == 0 || a.minK == 0) {
    return fail(ErrorCode::ValidationError,
                "a move atom with a zero step count can never move");
  }
  if (a.minK > a.maxK) {
    return fail(ErrorCode::ValidationError, "move atom has minK " +
                                                std::to_string(a.minK) + " above maxK " +
                                                std::to_string(a.maxK));
  }
  if (a.minK > 1 && a.mode == MoveMode::Leap) {
    return fail(
        ErrorCode::ValidationError,
        "an atom with minK > 1 must slide or hop; a leap has nothing to skip over");
  }
  if (a.mode == MoveMode::Hop && a.maxK != 1) {
    return fail(
        ErrorCode::Unsupported,
        "hop atoms are single-step for now; a repeating hopper needs its own rule");
  }
  return a;
}

std::string MoveAtom::toString() const {
  std::string out = "[";
  for (std::size_t i = 0; i < mags.size(); ++i) {
    if (i != 0) out += ',';
    out += std::to_string(mags[i]);
  }
  out += ",NULL]^";
  // minK appears only when it constrains, so the common notation stays the one
  // from the spec ("[1,2,NULL]^1"), but two atoms differing only in minK - a
  // pawn's push and its double step - never render identically.
  if (minK > 1) out += std::to_string(minK) + "..";
  out += maxK == kUnlimited ? "inf" : std::to_string(maxK);
  switch (mode) {
    case MoveMode::Slide:
      out += " slide";
      break;
    case MoveMode::Leap:
      out += " leap";
      break;
    case MoveMode::Hop:
      out += " hop";
      break;
  }
  if (capture == CapturePolicy::Must) out += " capture-only";
  if (capture == CapturePolicy::Cannot) out += " no-capture";
  if (oriented) out += " forward";
  if (fromRegion[0].active()) out += " from-home";
  return out;
}

bool operator==(const MoveAtom& a, const MoveAtom& b) {
  return a.mags == b.mags && a.minK == b.minK && a.maxK == b.maxK && a.mode == b.mode &&
         a.capture == b.capture && a.oriented == b.oriented &&
         a.fromRegion[0].axis == b.fromRegion[0].axis &&
         a.fromRegion[0].value == b.fromRegion[0].value &&
         a.leavesEnPassant == b.leavesEnPassant;
}

std::vector<Direction> expandAtom(const SmallVec<std::int16_t, kMaxDims>& mags,
                                  std::uint8_t dims) {
  std::vector<Direction> out;
  if (mags.size() > dims) return out;  // needs more axes than the board has

  std::set<std::array<std::int16_t, kMaxDims>> uniq;
  std::array<std::int16_t, kMaxDims> vec{};
  enumerate(mags, dims, 0, vec, 0, uniq);

  // std::set already orders lexicographically, which gives us determinism for
  // free - direction indices must not depend on the compiler or the platform.
  out.reserve(uniq.size());
  for (const auto& v : uniq) out.push_back(Direction::make(v, dims));
  return out;
}

std::vector<Direction> expandAtomOriented(const SmallVec<std::int16_t, kMaxDims>& mags,
                                          std::uint8_t dims, std::uint8_t orientAxis,
                                          Color color) {
  std::vector<Direction> out;
  const int want = color == Color::White ? 1 : -1;
  for (const Direction& d : expandAtom(mags, dims)) {
    const int comp = d.v[orientAxis];
    if (comp == 0) continue;
    if ((comp > 0 ? 1 : -1) == want) out.push_back(d);
  }
  return out;
}

std::uint64_t expectedDirectionCount(const SmallVec<std::int16_t, kMaxDims>& mags,
                                     std::uint8_t dims) {
  const auto r = static_cast<std::uint64_t>(mags.size());
  if (r > dims) return 0;

  // Ordered placements of r magnitudes onto distinct axes: P(dims, r).
  std::uint64_t perms = 1;
  for (std::uint64_t i = 0; i < r; ++i) perms *= (static_cast<std::uint64_t>(dims) - i);

  // Equal magnitudes make some of those placements identical, so divide by the
  // factorial of each repeat group. Missing this is the classic over-count.
  std::vector<std::int16_t> sorted(mags.begin(), mags.end());
  std::sort(sorted.begin(), sorted.end());
  std::uint64_t divisor = 1;
  std::size_t i = 0;
  while (i < sorted.size()) {
    std::size_t j = i;
    while (j < sorted.size() && sorted[j] == sorted[i]) ++j;
    divisor *= factorial(j - i);
    i = j;
  }

  return perms / divisor * (1ull << r);
}

}  // namespace cb
