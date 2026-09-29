// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/image_io.hpp"

#include <fstream>
#include <set>

namespace cb::render {

Image::Rgba Image::at(std::uint32_t x, std::uint32_t y) const {
  if (x >= width || y >= height) return {};
  const std::size_t i = (static_cast<std::size_t>(y) * width + x) * 4;
  if (i + 3 >= rgba.size()) return {};
  return Rgba{rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
}

std::size_t Image::distinctColors(std::size_t cap) const {
  std::set<std::uint32_t> seen;
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    const auto key = static_cast<std::uint32_t>(rgba[i]) << 24 |
                     static_cast<std::uint32_t>(rgba[i + 1]) << 16 |
                     static_cast<std::uint32_t>(rgba[i + 2]) << 8 |
                     static_cast<std::uint32_t>(rgba[i + 3]);
    seen.insert(key);
    if (seen.size() >= cap) break;
  }
  return seen.size();
}

int Image::coveragePerMille(Rgba background) const {
  const std::size_t total = static_cast<std::size_t>(width) * height;
  if (total == 0) return 0;
  std::size_t differing = 0;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      if (!(at(x, y) == background)) ++differing;
    }
  }
  return static_cast<int>(differing * 1000 / total);
}

Result<void> writePpm(const Image& img, const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return fail(ErrorCode::Internal, "cannot write '" + path.string() + "'");
  out << "P6\n" << img.width << ' ' << img.height << "\n255\n";
  for (std::uint32_t y = 0; y < img.height; ++y) {
    for (std::uint32_t x = 0; x < img.width; ++x) {
      const Image::Rgba p = img.at(x, y);
      out.put(static_cast<char>(p.r))
          .put(static_cast<char>(p.g))
          .put(static_cast<char>(p.b));
    }
  }
  if (!out) return fail(ErrorCode::Internal, "write failed for '" + path.string() + "'");
  return {};
}

}  // namespace cb::render
