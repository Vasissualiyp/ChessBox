// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "base/small_vec.hpp"
#include "space/dim_spec.hpp"

namespace cb::view {

/// How to flatten an N-dimensional lattice onto two or three spatial axes.
///
/// Two or three axes are drawn spatially. Every remaining axis becomes a *grid* axis:
/// the board is repeated as a lattice of sub-boards, which is the presentation 5D
/// chess uses for turn and timeline and which generalises to any number of extra axes
/// (ARCH section 10).
///
/// This is declarative on purpose - a new view style is a different config, not new
/// renderer code - and it lives below both the ASCII board and the Vulkan renderer so
/// there is exactly one definition of where a cell goes.
struct ViewConfig {
  SmallVec<std::uint8_t, 3> screenAxes;
  SmallVec<std::uint8_t, kMaxDims> gridAxes;
  /// Gap between neighbouring sub-boards, in cells.
  float gridGap{2.0f};
  /// Spacing along the third screen axis, as a multiple of the cell pitch.
  ///
  /// Levels of a 3-D board drawn one unit apart merge into an unreadable slab from any
  /// useful camera angle - the cells are far wider than the gap between layers. Pulling
  /// them apart is what makes a 3-D position legible, and it costs nothing but scene
  /// size.
  float depthSpacing{2.5f};

  /// Two screen axes for a 2-D board, three for anything higher, with the remaining
  /// axes becoming grid axes in declaration order.
  static ViewConfig forBoard(const DimSpec& d);

  [[nodiscard]] Result<void> validate(const DimSpec& d) const;
};

/// Where one cell is drawn, in world units.
struct Placement {
  CellId cell{kInvalidCell};
  float x{0};
  float y{0};
  float z{0};
  /// Index into the slice list, so a renderer can tint or frame whole sub-boards.
  std::uint32_t slice{0};
};

/// One sub-board: the coordinates held fixed on the grid axes.
struct Slice {
  SmallVec<std::int16_t, kMaxDims> at;
  float originX{0};
  float originY{0};
  [[nodiscard]] std::string label(const DimSpec& d, const ViewConfig& cfg) const;
};

/// Enumerate the sub-boards, lowest grid axis varying fastest.
std::vector<Slice> enumerateSlices(const DimSpec& d, const ViewConfig& cfg);

/// Place every cell of the lattice. Guaranteed injective in world space, which is
/// asserted as a property test - two cells sharing a position would be unclickable
/// and unreadable.
std::vector<Placement> layout(const DimSpec& d, const ViewConfig& cfg);

/// Extent of the whole laid-out scene, for framing a camera.
struct Bounds {
  float minX{0}, minY{0}, minZ{0};
  float maxX{0}, maxY{0}, maxZ{0};
  [[nodiscard]] float centerX() const { return (minX + maxX) * 0.5f; }
  [[nodiscard]] float centerY() const { return (minY + maxY) * 0.5f; }
  [[nodiscard]] float centerZ() const { return (minZ + maxZ) * 0.5f; }
  [[nodiscard]] float radius() const;
};

Bounds boundsOf(const std::vector<Placement>& placements);

}  // namespace cb::view
