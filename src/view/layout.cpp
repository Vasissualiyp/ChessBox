// SPDX-License-Identifier: GPL-3.0-or-later
#include "view/layout.hpp"

#include <algorithm>
#include <cmath>

namespace cb::view {

ViewConfig ViewConfig::forBoard(const DimSpec& d) {
  ViewConfig cfg;
  const std::uint8_t n = d.dims();
  const std::uint8_t spatial = n >= 3 ? 3 : n;
  for (std::uint8_t a = 0; a < spatial; ++a) cfg.screenAxes.push(a);
  for (std::uint8_t a = spatial; a < n; ++a) cfg.gridAxes.push(a);
  return cfg;
}

Result<void> ViewConfig::validate(const DimSpec& d) const {
  if (screenAxes.empty() || screenAxes.size() > 3) {
    return fail(ErrorCode::ValidationError, "a view needs one to three screen axes");
  }
  std::vector<bool> used(kMaxDims, false);
  for (std::uint8_t a : screenAxes) {
    if (a >= d.dims()) {
      return fail(ErrorCode::ValidationError,
                  "screen axis " + std::to_string(a) + " does not exist on this board");
    }
    if (used[a]) {
      return fail(ErrorCode::ValidationError,
                  "axis " + std::to_string(a) + " is used twice");
    }
    used[a] = true;
  }
  for (std::uint8_t a : gridAxes) {
    if (a >= d.dims()) {
      return fail(ErrorCode::ValidationError,
                  "grid axis " + std::to_string(a) + " does not exist on this board");
    }
    if (used[a]) {
      return fail(ErrorCode::ValidationError,
                  "axis " + std::to_string(a) + " is used twice");
    }
    used[a] = true;
  }
  for (std::uint8_t a = 0; a < d.dims(); ++a) {
    if (!used[a]) {
      return fail(ErrorCode::ValidationError,
                  "axis '" + d.name(a) +
                      "' is neither a screen axis nor a grid axis, so its "
                      "cells would be drawn on top of each other");
    }
  }
  if (gridGap < 0) return fail(ErrorCode::ValidationError, "gridGap cannot be negative");
  return {};
}

std::string Slice::label(const DimSpec& d, const ViewConfig& cfg) const {
  std::string s;
  for (std::size_t i = 0; i < cfg.gridAxes.size(); ++i) {
    if (i != 0) s += ' ';
    s += d.name(cfg.gridAxes[i]);
    s += '=';
    s += std::to_string(at[i]);
  }
  return s;
}

namespace {

/// Size of one sub-board along a screen axis, including the gap that separates it
/// from its neighbour.
float pitch(const DimSpec& d, const ViewConfig& cfg, std::size_t screenIndex) {
  if (screenIndex >= cfg.screenAxes.size()) return 1.0f + cfg.gridGap;
  const auto extent = static_cast<float>(d.extent(cfg.screenAxes[screenIndex]));
  return extent + cfg.gridGap;
}

}  // namespace

std::vector<Slice> enumerateSlices(const DimSpec& d, const ViewConfig& cfg) {
  std::vector<Slice> out;
  const std::size_t g = cfg.gridAxes.size();

  Slice cur;
  cur.at.resize(g);
  for (std::size_t i = 0; i < g; ++i) cur.at[i] = 0;

  // Grid axes alternate between laying out horizontally and vertically, so a 4-D
  // board reads as a 2-D grid of boards rather than one very long row - the same
  // arrangement 5D chess uses for turn and timeline.
  const float pitchX = pitch(d, cfg, 0);
  const float pitchY = pitch(d, cfg, 1);

  for (;;) {
    Slice s = cur;
    float ox = 0;
    float oy = 0;
    for (std::size_t i = 0; i < g; ++i) {
      // Grid axes alternate between the two screen directions, so a 4-D board reads as
      // a 2-D grid of boards rather than one very long row. `gridVertical` swaps which
      // direction comes first, which is the "column of boards" arrangement.
      const bool alongX = (i % 2 == 0) != cfg.gridVertical;
      float block = alongX ? pitchX : pitchY;
      // Later grid axes step by whole blocks of the earlier ones with the same
      // direction, so no two slices can ever land on the same origin.
      for (std::size_t j = 0; j < i; ++j) {
        if (((j % 2 == 0) != cfg.gridVertical) == alongX) {
          block *= static_cast<float>(d.extent(cfg.gridAxes[j]));
        }
      }
      if (alongX) {
        ox += static_cast<float>(s.at[i]) * block;
      } else {
        oy += static_cast<float>(s.at[i]) * block;
      }
    }
    s.originX = ox;
    s.originY = oy;
    out.push_back(s);

    if (g == 0) break;
    std::size_t i = 0;
    for (;; ++i) {
      if (i >= g) return out;
      if (cur.at[i] + 1 < d.extent(cfg.gridAxes[i])) {
        ++cur.at[i];
        break;
      }
      cur.at[i] = 0;
    }
  }
  return out;
}

std::vector<Placement> layout(const DimSpec& d, const ViewConfig& cfg) {
  std::vector<Placement> out;
  const auto slices = enumerateSlices(d, cfg);
  out.reserve(static_cast<std::size_t>(d.cellCount()));

  for (std::uint32_t si = 0; si < slices.size(); ++si) {
    const Slice& s = slices[si];
    // Walk every cell of this sub-board by odometer over the screen axes.
    SmallVec<std::int16_t, 3> at;
    at.resize(cfg.screenAxes.size());
    for (std::size_t i = 0; i < at.size(); ++i) at[i] = 0;

    for (;;) {
      Coord c(d.dims());
      for (std::size_t i = 0; i < cfg.screenAxes.size(); ++i)
        c.c[cfg.screenAxes[i]] = at[i];
      for (std::size_t i = 0; i < cfg.gridAxes.size(); ++i)
        c.c[cfg.gridAxes[i]] = s.at[i];

      Placement p;
      p.cell = d.toCell(c);
      p.x = s.originX + static_cast<float>(at.size() > 0 ? at[0] : 0);
      p.y = s.originY + static_cast<float>(at.size() > 1 ? at[1] : 0);
      p.z = static_cast<float>(at.size() > 2 ? at[2] : 0) * cfg.depthSpacing;
      p.slice = si;
      out.push_back(p);

      if (at.empty()) break;
      std::size_t i = 0;
      bool done = false;
      for (;; ++i) {
        if (i >= at.size()) {
          done = true;
          break;
        }
        if (at[i] + 1 < d.extent(cfg.screenAxes[i])) {
          ++at[i];
          break;
        }
        at[i] = 0;
      }
      if (done) break;
    }
  }
  return out;
}

float Bounds::radius() const {
  const float dx = maxX - minX;
  const float dy = maxY - minY;
  const float dz = maxZ - minZ;
  return 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
}

Bounds boundsOf(const std::vector<Placement>& placements) {
  Bounds b;
  if (placements.empty()) return b;
  b.minX = b.maxX = placements.front().x;
  b.minY = b.maxY = placements.front().y;
  b.minZ = b.maxZ = placements.front().z;
  for (const Placement& p : placements) {
    b.minX = std::min(b.minX, p.x);
    b.maxX = std::max(b.maxX, p.x);
    b.minY = std::min(b.minY, p.y);
    b.maxY = std::max(b.maxY, p.y);
    b.minZ = std::min(b.minZ, p.z);
    b.maxZ = std::max(b.maxZ, p.z);
  }
  return b;
}

}  // namespace cb::view
