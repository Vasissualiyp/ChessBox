// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/embed.hpp"

#include <cstddef>
#include <string_view>

namespace cb::app {

Position embedOnto(const Position& from, const VariantSpec& fromVariant,
                   const VariantSpec& toVariant) {
  const DimSpec& fd = fromVariant.dims;
  const DimSpec& td = toVariant.dims;
  Position out(toVariant);
  for (CellId cell = 0; cell < from.cellCount(); ++cell) {
    if (from.isEmpty(cell)) continue;
    const Piece piece = from.at(cell);
    const std::string_view name = fromVariant.pieces[piece.type].name;
    const PieceTypeId type = toVariant.findPiece(name);
    if (type == kNoPiece) continue;  // the target has no such piece kind to carry it on
    const Coord src = fd.toCoord(cell);
    Coord dst(td.dims());
    for (std::uint8_t a = 0; a < td.dims(); ++a) {
      const int srcAxis = fd.axisIndex(td.name(a));
      // A shared axis keeps its coordinate; a new one lands on slice 0.
      dst[a] = srcAxis >= 0 ? src.c[static_cast<std::size_t>(srcAxis)] : 0;
    }
    out.place(td.toCell(dst), type, piece.colorOf());
  }
  out.setSideToMove(from.sideToMove());
  return out;
}

}  // namespace cb::app
