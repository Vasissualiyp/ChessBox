// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include "base/result.hpp"

namespace cb::render {

/// A rendered image in host memory, as tightly packed RGBA8 rows.
struct Image {
  std::uint32_t width{0};
  std::uint32_t height{0};
  std::vector<std::uint8_t> rgba;

  struct Rgba {
    std::uint8_t r{0}, g{0}, b{0}, a{0};
    friend bool operator==(const Rgba&, const Rgba&) = default;
  };

  [[nodiscard]] Rgba at(std::uint32_t x, std::uint32_t y) const;
  /// Number of distinct colours, capped so a pathological image cannot blow up memory.
  [[nodiscard]] std::size_t distinctColors(std::size_t cap = 4096) const;
  /// Fraction of pixels differing from `background`, in parts per thousand. A cheap,
  /// robust "did anything actually get drawn" measure.
  [[nodiscard]] int coveragePerMille(Rgba background) const;
  /// Population variance of luminance. A blank fill is 0; a board is far from it - a
  /// cheap "is there a picture here" that a solid colour cannot fake.
  [[nodiscard]] double luminanceVariance() const;
  /// Pixels whose channels are far from grey. The board reserves saturated colour for
  /// geometry that is not flat, so this is near zero on a plain board and non-zero where
  /// there are seams or other non-flat geometry. `spread` is the least max-minus-min
  /// channel difference counted, and `floorValue` the least brightness.
  [[nodiscard]] std::size_t saturatedPixels(int spread = 60, int floorValue = 120) const;
};

/// Write a PPM. Deliberately not PNG: PPM needs no compression library, and the only
/// consumer is a human looking at a debug capture. Tests assert on pixels, not on files.
Result<void> writePpm(const Image& img, const std::filesystem::path& path);

}  // namespace cb::render
