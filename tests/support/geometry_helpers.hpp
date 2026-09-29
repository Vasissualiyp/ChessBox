// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <set>
#include <string>
#include <vector>

#include "geometry/geometry.hpp"
#include "space/dim_spec.hpp"

namespace cb::test {

inline DimSpec makeDims(std::initializer_list<int> extents) {
  std::vector<AxisDecl> axes;
  int i = 0;
  for (int e : extents) {
    axes.push_back(AxisDecl{e, AxisKind::Spatial, "a" + std::to_string(i++)});
  }
  return DimSpec::create(axes).value();
}

inline Direction dir(const DimSpec& d, std::initializer_list<int> v) {
  std::array<std::int16_t, kMaxDims> a{};
  std::size_t i = 0;
  for (int x : v) a[i++] = static_cast<std::int16_t>(x);
  return Direction::make(a, d.dims());
}

/// Walk a ray until it leaves the board or returns to its exact starting state
/// (same cell AND same direction). The returned length is the orbit length, which
/// is a sharp fingerprint of a surface's topology: a rook's orbit on a torus rank
/// is the extent, on a Klein bottle it can be twice that, and on a box it is
/// simply the distance to the wall.
struct Orbit {
  std::vector<CellId> cells;
  bool closed{false};  ///< true if it returned to the start state (a cycle)
  bool orientationFlipped{false};
};

inline Orbit walk(const Geometry& g, CellId from, const Direction& d,
                  int maxSteps = 4096) {
  Orbit out;
  Walker w = g.start(from, d);
  const Direction startDir = d;
  for (int i = 0; i < maxSteps; ++i) {
    if (!g.step(w)) break;
    if (w.cell == from && w.dir == startDir) {
      out.closed = true;
      break;
    }
    out.cells.push_back(w.cell);
    if (w.cell == from && !(w.dir == startDir)) out.orientationFlipped = true;
  }
  return out;
}

/// Every cell reachable from `from` by repeatedly stepping any of `dirs`.
inline std::set<CellId> reachable(const Geometry& g, CellId from,
                                  const std::vector<Direction>& dirs) {
  std::set<CellId> seen{from};
  std::vector<CellId> stack{from};
  while (!stack.empty()) {
    const CellId c = stack.back();
    stack.pop_back();
    for (const Direction& d : dirs) {
      Walker w = g.start(c, d);
      if (!g.step(w)) continue;
      if (seen.insert(w.cell).second) stack.push_back(w.cell);
    }
  }
  return seen;
}

}  // namespace cb::test
