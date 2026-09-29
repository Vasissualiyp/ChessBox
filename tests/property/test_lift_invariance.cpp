// SPDX-License-Identifier: GPL-3.0-or-later
// Dimension-lift invariance (M2.2), the single most valuable test in the project.
//
// Embedding a D-dimensional variant into D+1 dimensions with extent 1 on the new
// axis must produce an *identical game*: the same legal moves at every node and
// therefore the same perft. One assertion catches almost every class of
// dimension-handling error at once - an off-by-one in expansion, a sign mistake,
// stride arithmetic, the wrong orientation axis, a region predicate reading the
// wrong coordinate - because all of them break the equality.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "io/notation.hpp"
#include "movegen/movegen.hpp"
#include "support/random_variant.hpp"
#include "variant/standard.hpp"

using namespace cb;

namespace {

/// The same variant with `extra` axes of extent 1 appended.
VariantSpec lift(const VariantSpec& base, int extra) {
  VariantSpec v;
  v.name = base.name + "-lifted" + std::to_string(extra);

  std::vector<AxisDecl> axes;
  for (std::uint8_t a = 0; a < base.dims.dims(); ++a) {
    axes.push_back(AxisDecl{base.dims.extent(a), base.dims.kind(a), base.dims.name(a)});
  }
  for (int i = 0; i < extra; ++i) {
    axes.push_back(AxisDecl{1, AxisKind::Spatial, "lift" + std::to_string(i)});
  }
  v.dims = DimSpec::create(axes).value();

  // The identifications carry over unchanged; the new axes are simply walls, which
  // an extent-1 axis makes unreachable anyway.
  std::vector<IdentDecl> idents;
  for (std::uint8_t a = 0; a < base.dims.dims(); ++a) {
    for (const Side s : {Side::Min, Side::Max}) {
      const Transform* t = base.geom.faceTransform(a, s);
      if (t == nullptr) continue;
      // Reconstructing a declaration from a transform is not possible in general,
      // so lifting is applied only to box variants here; the geometry-specific
      // invariants live in the geometry tests.
      (void)t;
      return VariantSpec{};
    }
  }
  v.geom = Geometry::create(v.dims, idents).value();

  v.pieces = base.pieces;
  v.start = base.start;
  v.startSideToMove = base.startSideToMove;
  v.orientationAxis = base.orientationAxis;
  for (int ci = 0; ci < kNumColors; ++ci) v.promotion[ci] = base.promotion[ci];
  v.enPassant = base.enPassant;
  v.stalemate = base.stalemate;
  v.halfmoveDrawLimit = base.halfmoveDrawLimit;

  // Starting coordinates gain zeros on the new axes.
  for (StartPiece& sp : v.start) sp.at.n = v.dims.dims();
  // Atom spans are recomputed by finalize(); clear what the base had resolved.
  for (PieceTypeDef& p : v.pieces) {
    for (MoveAtom& a : p.atoms) {
      for (int ci = 0; ci < kNumColors; ++ci) {
        a.dirBegin[ci] = 0;
        a.dirEnd[ci] = 0;
        a.threatBegin[ci] = 0;
        a.threatEnd[ci] = 0;
      }
    }
  }

  // Castling cell ids are flat indices, and appending axes of extent 1 leaves
  // every existing index unchanged - the new strides multiply by 1.
  v.castles = base.castles;

  const auto ok = v.finalize();
  REQUIRE(ok.has_value());
  return v;
}

}  // namespace

TEST_CASE("standard chess is unchanged by lifting it into higher dimensions",
          "[property][dims]") {
  const auto base = makeStandardChess();
  REQUIRE(base.has_value());
  Position bp = Position::startPosition(*base);
  const MoveGen bg(*base);
  const std::uint64_t reference3 = bg.perft(bp, 3);
  REQUIRE(reference3 == 8902);

  for (int extra = 1; extra + 2 <= kMaxDims; ++extra) {
    const auto lifted = makeStandardChessLifted(extra);
    REQUIRE(lifted.has_value());
    CAPTURE(extra, lifted->dims.dims());
    REQUIRE(lifted->dims.cellCount() == base->dims.cellCount());

    Position lp = Position::startPosition(*lifted);
    const MoveGen lg(*lifted);
    REQUIRE(lg.perft(lp, 1) == 20);
    REQUIRE(lg.perft(lp, 2) == 400);
    REQUIRE(lg.perft(lp, 3) == reference3);
  }
}

TEST_CASE("lifted standard chess generates the same moves, move for move",
          "[property][dims]") {
  // Equal counts could in principle hide two different move sets. This checks the
  // moves themselves, which also pins the cell numbering: an extent-1 axis must not
  // perturb a single flat index.
  const auto base = makeStandardChess();
  const auto lifted = makeStandardChessLifted(3);
  Position bp = Position::startPosition(*base);
  Position lp = Position::startPosition(*lifted);
  MoveList bm(base->moveUpperBound());
  MoveList lm(lifted->moveUpperBound());
  MoveGen(*base).generateLegal(bp, bm);
  MoveGen(*lifted).generateLegal(lp, lm);

  REQUIRE(bm.size() == lm.size());
  for (std::size_t i = 0; i < bm.size(); ++i) {
    CAPTURE(i);
    REQUIRE(bm[i].from == lm[i].from);
    REQUIRE(bm[i].to == lm[i].to);
    REQUIRE(bm[i].flags == lm[i].flags);
  }
}

TEST_CASE("a random box variant is unchanged by lifting", "[property][dims]") {
  const auto seed = static_cast<std::uint64_t>(GENERATE(range(1, 40)));
  test::RandomVariantOptions opts;
  opts.allowGeometry = false;  // lifting a gluing is a separate question (M3)
  opts.maxDims = 3;
  const VariantSpec base = test::randomVariant(seed, opts);
  if (base.dims.dims() >= kMaxDims) return;

  const MoveGen bg(base);
  Position bp = Position::startPosition(base);
  const std::uint64_t reference = bg.perft(bp, 2);

  for (int extra = 1; base.dims.dims() + extra <= kMaxDims; ++extra) {
    const VariantSpec lifted = lift(base, extra);
    if (lifted.dims.cellCount() == 0) return;  // lift() declined this variant
    CAPTURE(seed, extra, base.dims.dims());
    REQUIRE(lifted.dims.cellCount() == base.dims.cellCount());
    Position lp = Position::startPosition(lifted);
    REQUIRE(MoveGen(lifted).perft(lp, 2) == reference);
  }
}
