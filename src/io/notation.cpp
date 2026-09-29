// SPDX-License-Identifier: GPL-3.0-or-later
#include "io/notation.hpp"

#include <charconv>
#include <vector>

namespace cb {
namespace {

bool algebraic(const DimSpec& d) {
  return d.dims() == 2 && d.extent(0) <= 26 && d.extent(1) <= 9;
}

}  // namespace

std::string cellName(const DimSpec& d, CellId c) {
  if (c == kInvalidCell) return "-";
  const Coord p = d.toCoord(c);
  if (algebraic(d)) {
    std::string s;
    s += static_cast<char>('a' + p.c[0]);
    s += static_cast<char>('1' + p.c[1]);
    return s;
  }
  return p.toString();
}

Result<CellId> parseCell(const DimSpec& d, std::string_view text) {
  if (text.empty()) return fail(ErrorCode::ParseError, "empty cell name");

  if (text.front() == '(') {
    if (text.back() != ')') {
      return fail(ErrorCode::ParseError,
                  "cell '" + std::string(text) + "' is missing its ')'");
    }
    Coord p(d.dims());
    std::size_t axis = 0;
    std::size_t i = 1;
    while (i < text.size() - 1) {
      std::size_t j = text.find(',', i);
      if (j == std::string_view::npos || j > text.size() - 1) j = text.size() - 1;
      int value = 0;
      const auto* first = text.data() + i;
      const auto* last = text.data() + j;
      const auto res = std::from_chars(first, last, value);
      if (res.ec != std::errc{} || res.ptr != last) {
        return fail(ErrorCode::ParseError,
                    "cell '" + std::string(text) + "' has a non-numeric coordinate");
      }
      if (axis >= d.dims()) {
        return fail(ErrorCode::ParseError,
                    "cell '" + std::string(text) +
                        "' has more coordinates than the board has axes");
      }
      p.c[axis++] = static_cast<std::int16_t>(value);
      i = j + 1;
    }
    if (axis != d.dims()) {
      return fail(ErrorCode::ParseError, "cell '" + std::string(text) + "' needs " +
                                             std::to_string(d.dims()) + " coordinates");
    }
    if (!d.inRange(p)) {
      return fail(ErrorCode::OutOfRange, "cell " + p.toString() + " is off the board");
    }
    return d.toCell(p);
  }

  if (!algebraic(d)) {
    return fail(
        ErrorCode::ParseError,
        "this board needs the '(x,y,...)' cell form, not '" + std::string(text) + "'");
  }
  if (text.size() != 2 || text[0] < 'a' || text[1] < '1') {
    return fail(ErrorCode::ParseError, "'" + std::string(text) + "' is not a cell name");
  }
  Coord p(2);
  p.c[0] = static_cast<std::int16_t>(text[0] - 'a');
  p.c[1] = static_cast<std::int16_t>(text[1] - '1');
  if (!d.inRange(p)) {
    return fail(ErrorCode::OutOfRange,
                "cell '" + std::string(text) + "' is off the board");
  }
  return d.toCell(p);
}

std::string moveText(const VariantSpec& v, const Move& m) {
  std::string s = cellName(v.dims, m.from);
  const bool alg = v.dims.dims() == 2 && v.dims.extent(0) <= 26 && v.dims.extent(1) <= 9;
  if (!alg) s += '-';
  s += cellName(v.dims, m.to);
  if (has(m.flags, MoveFlag::Promotion) && m.promoteTo != kNoPiece) {
    s += static_cast<char>(std::tolower(v.pieces[m.promoteTo].symbol));
  }
  return s;
}

}  // namespace cb
