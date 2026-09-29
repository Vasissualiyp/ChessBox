// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "base/result.hpp"
#include "base/zobrist.hpp"
#include "geometry/geometry.hpp"
#include "pieces/atom.hpp"
#include "space/dim_spec.hpp"

namespace cb {

using PieceTypeId = std::uint16_t;
constexpr PieceTypeId kNoPiece = 0;  ///< type 0 means "empty cell"

/// A piece kind. Everything that distinguishes a knight from a nightrider lives
/// in `atoms`; nothing about movement is hardcoded anywhere else.
struct PieceTypeDef {
  std::string name;
  char symbol{'?'};  ///< upper case; lower case denotes Black in notation
  bool royal{false}; ///< losing or exposing it ends the game, per the variant
  std::vector<MoveAtom> atoms;
  std::vector<PieceTypeId> promotesTo;  ///< empty means this piece never promotes
  /// Whether moving this piece resets the draw clock. Declared rather than
  /// inferred: "it is a pawn" is not a concept the engine has.
  bool resetsDrawClock{false};
};

/// A multi-piece move template. Castling is the familiar instance, but the shape
/// is general: an ordered list of displacements plus conditions, which works in
/// any dimension and on any topology.
struct CastleTemplate {
  std::string name;  ///< the FEN letter for standard chess ("K", "Q", "k", "q")
  Color color{Color::White};
  CellId kingFrom{kInvalidCell};
  CellId kingTo{kInvalidCell};
  CellId rookFrom{kInvalidCell};
  CellId rookTo{kInvalidCell};
  std::vector<CellId> mustBeEmpty;
  std::vector<CellId> mustBeSafe;  ///< not attacked by the opponent
  std::uint8_t rightsBit{0};
};

struct StartPiece {
  Coord at;
  PieceTypeId type{kNoPiece};
  Color color{Color::White};
};

/// What to do when the side to move has no legal move and is not in check.
enum class StalematePolicy : std::uint8_t { Draw, Loss, Win };

/// A fully resolved variant: everything L4..L8 needs, precomputed once at load.
/// Nothing below L9 ever parses text.
class VariantSpec {
 public:
  std::string name;
  DimSpec dims;
  Geometry geom;

  /// Index 0 is the reserved empty entry, so a PieceTypeId indexes directly.
  std::vector<PieceTypeDef> pieces;
  std::vector<CastleTemplate> castles;
  std::vector<StartPiece> start;
  Color startSideToMove{Color::White};

  /// The axis along which "forward" is defined, and therefore the axis pawn-like
  /// pieces and promotion regions refer to.
  int orientationAxis{-1};
  Region promotion[kNumColors]{};
  bool enPassant{false};
  StalematePolicy stalemate{StalematePolicy::Draw};
  /// 0 disables the rule; standard chess uses 100 half-moves.
  int halfmoveDrawLimit{0};

  /// Global direction table. Atom spans index into this.
  std::vector<Direction> dirTable;

  /// Hash keys, derived from the variant id so that a position's hash is
  /// reproducible across runs and hosts (ARCH section 11). Filled by finalize().
  Zobrist zob;

  /// Resolve atoms into the direction table and validate the whole spec. Call
  /// exactly once, after the declarative fields are filled in.
  [[nodiscard]] Result<void> finalize();

  [[nodiscard]] bool finalized() const noexcept { return finalized_; }
  [[nodiscard]] std::uint64_t variantId() const noexcept { return variantId_; }
  [[nodiscard]] std::uint32_t pieceCodes() const noexcept {
    return static_cast<std::uint32_t>(pieces.size()) * kNumColors;
  }
  [[nodiscard]] PieceTypeId findPiece(std::string_view name) const;
  [[nodiscard]] PieceTypeId findPieceBySymbol(char upper) const;

  /// Upper bound on moves generated from any one position, used to size move
  /// buffers once so that movegen itself never allocates.
  [[nodiscard]] std::uint32_t moveUpperBound() const noexcept { return moveBound_; }

  /// Total directions across all pieces and colours; the budget check subject.
  [[nodiscard]] std::size_t directionCount() const noexcept { return dirTable.size(); }

 private:
  bool finalized_{false};
  std::uint64_t variantId_{0};
  std::uint32_t moveBound_{0};
};

/// Budget on the size of the global direction table. [1,2,3] alone is 2688
/// directions at 8 dimensions, so this has to be a real check with a real
/// message rather than an assert (M2.3).
constexpr std::size_t kMaxDirections = 1 << 16;

}  // namespace cb
