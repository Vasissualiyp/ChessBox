// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "base/small_vec.hpp"
#include "space/dim_spec.hpp"

namespace cb {

/// Separator written when the group along `axis` rolls over. Axis 1 uses '/', to
/// match standard FEN's rank separator; higher axes get progressively longer '|'
/// runs, so a 4-D board reads as slabs of boards of ranks.
inline std::string fenSeparatorFor(std::uint8_t axis) {
  if (axis == 1) return "/";
  return std::string(static_cast<std::size_t>(axis) - 1, '|');
}

/// Visit every cell in FEN-N order: axis 1 descending (so rank 8 comes first),
/// axis 0 ascending, higher axes ascending. `boundary(axis)` is called when the
/// coordinate on `axis - 1` rolls over - but never after the final cell, so no
/// trailing separator is produced.
///
/// One definition, used by both the serializer and the variant loader: the order
/// is part of the format, and two implementations of it would eventually disagree.
template <class Visit, class Boundary>
void forEachInFenOrder(const DimSpec& d, Visit visit, Boundary boundary) {
  const std::uint8_t n = d.dims();
  Coord p(n);
  const auto reset = [&](std::uint8_t a) {
    p.c[a] = a == 1 ? static_cast<std::int16_t>(d.extent(1) - 1) : 0;
  };
  for (std::uint8_t a = 0; a < n; ++a) reset(a);

  for (;;) {
    visit(d.toCell(p));

    SmallVec<std::uint8_t, kMaxDims + 1> pending;
    bool done = false;
    for (std::uint8_t a = 0;; ++a) {
      if (a >= n) {
        done = true;
        break;
      }
      if (a == 1) {
        if (p.c[1] > 0) {
          --p.c[1];
          break;
        }
      } else if (p.c[a] + 1 < d.extent(a)) {
        ++p.c[a];
        break;
      }
      reset(a);
      pending.push(static_cast<std::uint8_t>(a + 1));
    }
    // Separators are flushed only once continuation is certain, which is what
    // keeps a trailing '/' out of the output.
    if (done) return;
    for (std::uint8_t axis : pending) boundary(axis);
  }
}

}  // namespace cb
