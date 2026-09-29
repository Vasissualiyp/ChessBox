// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/ascii_board.hpp"

#include <vector>

namespace cb {
namespace {

char glyph(const VariantSpec& v, const Piece& piece, char empty) {
  if (piece.empty()) return empty;
  const char sym = v.pieces[piece.type].symbol;
  return piece.colorOf() == Color::White ? static_cast<char>(std::toupper(sym))
                                         : static_cast<char>(std::tolower(sym));
}

/// Row label: 1-based like a chessboard when the axis is short enough for a single
/// digit, and the raw coordinate otherwise.
std::string rowLabel(const DimSpec& d, std::uint8_t axis, int value) {
  if (d.extent(axis) <= 9) return std::to_string(value + 1);
  return std::to_string(value);
}

}  // namespace

std::string renderBoard(const Position& p, const AsciiView& view) {
  const VariantSpec& v = p.variant();
  const DimSpec& d = v.dims;
  const std::uint8_t n = d.dims();

  const std::uint8_t ax = view.axisX < n ? view.axisX : 0;
  const std::uint8_t ay = view.axisY < n && view.axisY != ax ? view.axisY : ax;

  // Axes that are neither screen axis become the outer slice loop.
  std::vector<std::uint8_t> outer;
  for (std::uint8_t a = 0; a < n; ++a) {
    if (a != ax && a != ay) outer.push_back(a);
  }
  std::vector<std::int16_t> at(n, 0);

  std::string out;
  const int rowLabelWidth = d.extent(ay) <= 9 ? 1 : 3;

  const auto renderSlice = [&] {
    if (!outer.empty() && view.labels) {
      out += "slice";
      for (std::uint8_t a : outer) {
        out += ' ';
        out += d.name(a);
        out += '=';
        out += std::to_string(at[a]);
      }
      out += '\n';
    }
    for (int y = d.extent(ay) - 1; y >= 0; --y) {
      if (view.labels) {
        const std::string label = rowLabel(d, ay, y);
        out += std::string(static_cast<std::size_t>(rowLabelWidth) - label.size(), ' ');
        out += label;
        out += ' ';
      }
      for (int x = 0; x < d.extent(ax); ++x) {
        Coord c(n);
        for (std::uint8_t a = 0; a < n; ++a) c.c[a] = at[a];
        c.c[ax] = static_cast<std::int16_t>(x);
        c.c[ay] = static_cast<std::int16_t>(y);
        out += glyph(v, p.at(d.toCell(c)), view.empty);
        if (x + 1 < d.extent(ax)) out += ' ';
      }
      out += '\n';
    }
    if (view.labels && d.extent(ax) <= 26) {
      out += std::string(static_cast<std::size_t>(rowLabelWidth) + 1, ' ');
      for (int x = 0; x < d.extent(ax); ++x) {
        out += static_cast<char>('a' + x);
        if (x + 1 < d.extent(ax)) out += ' ';
      }
      out += '\n';
    }
  };

  if (outer.empty()) {
    renderSlice();
    return out;
  }

  // Odometer over the non-screen axes, lowest axis fastest.
  for (;;) {
    renderSlice();
    std::size_t i = 0;
    for (;; ++i) {
      if (i >= outer.size()) return out;
      const std::uint8_t a = outer[i];
      if (at[a] + 1 < d.extent(a)) {
        ++at[a];
        break;
      }
      at[a] = 0;
    }
    out += '\n';
  }
}

}  // namespace cb
