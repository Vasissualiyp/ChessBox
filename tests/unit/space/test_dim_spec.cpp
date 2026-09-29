// SPDX-License-Identifier: GPL-3.0-or-later
#include "space/dim_spec.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace cb;

namespace {
std::vector<AxisDecl> box(std::initializer_list<int> extents) {
  std::vector<AxisDecl> out;
  int i = 0;
  for (int e : extents) {
    out.push_back(
        AxisDecl{e, AxisKind::Spatial, std::string(1, static_cast<char>('x' + i++))});
  }
  return out;
}
}  // namespace

TEST_CASE("DimSpec computes row-major strides and cell count", "[unit][space]") {
  const auto axes = box({8, 8});
  const auto d = DimSpec::create(axes);
  REQUIRE(d.has_value());
  REQUIRE(d->dims() == 2);
  REQUIRE(d->stride(0) == 1);
  REQUIRE(d->stride(1) == 8);
  REQUIRE(d->cellCount() == 64);
}

TEST_CASE("DimSpec maps coordinates to cells and back", "[unit][space]") {
  const auto axes = box({4, 5, 6});
  const auto d = DimSpec::create(axes).value();
  REQUIRE(d.cellCount() == 120);
  const Coord p = Coord::of({3, 1, 4});
  const CellId c = d.toCell(p);
  REQUIRE(c == 3 + 1 * 4 + 4 * 20);
  REQUIRE(d.toCoord(c) == p);
}

TEST_CASE("DimSpec rejects a degenerate or absurd board", "[unit][space]") {
  SECTION("no axes") {
    const auto r = DimSpec::create({});
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::ValidationError);
  }
  SECTION("zero extent") {
    const auto axes = box({8, 0});
    const auto r = DimSpec::create(axes);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().message.find("non-positive") != std::string::npos);
  }
  SECTION("too many axes") {
    std::vector<AxisDecl> axes;
    for (int i = 0; i < kMaxDims + 1; ++i) {
      axes.push_back(AxisDecl{2, AxisKind::Spatial, "a" + std::to_string(i)});
    }
    const auto r = DimSpec::create(axes);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::BudgetExceeded);
  }
  SECTION("cell count overflows the budget instead of wrapping") {
    // 8 axes of extent 40 is 6.5e12 cells; must be a validation error, never a
    // silent 32-bit wrap followed by a bad_alloc.
    std::vector<AxisDecl> axes;
    for (int i = 0; i < kMaxDims; ++i) {
      axes.push_back(AxisDecl{40, AxisKind::Spatial, "a" + std::to_string(i)});
    }
    const auto r = DimSpec::create(axes);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == ErrorCode::BudgetExceeded);
  }
  SECTION("duplicate axis names") {
    std::vector<AxisDecl> axes{{8, AxisKind::Spatial, "x"}, {8, AxisKind::Spatial, "x"}};
    const auto r = DimSpec::create(axes);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().message.find("duplicate") != std::string::npos);
  }
}

TEST_CASE("DimSpec finds axes by name and reports kinds", "[unit][space]") {
  std::vector<AxisDecl> axes{{8, AxisKind::Spatial, "x"},
                             {8, AxisKind::Spatial, "y"},
                             {4, AxisKind::Temporal, "t"},
                             {3, AxisKind::Multiverse, "l"}};
  const auto d = DimSpec::create(axes).value();
  REQUIRE(d.axisIndex("t") == 2);
  REQUIRE(d.axisIndex("nope") == -1);
  REQUIRE(d.kind(2) == AxisKind::Temporal);
  REQUIRE(d.kind(3) == AxisKind::Multiverse);
  // Kinds are metadata below L7: they must not affect the lattice shape at all.
  REQUIRE(d.cellCount() == 8 * 8 * 4 * 3);
}

TEST_CASE("DimSpec::inRange guards the board box", "[unit][space]") {
  const auto axes = box({3, 4});
  const auto d = DimSpec::create(axes).value();
  REQUIRE(d.inRange(Coord::of({0, 0})));
  REQUIRE(d.inRange(Coord::of({2, 3})));
  REQUIRE_FALSE(d.inRange(Coord::of({3, 3})));
  REQUIRE_FALSE(d.inRange(Coord::of({0, -1})));
  REQUIRE_FALSE(d.inRange(Coord::of({0, 0, 0})));  // wrong dimension count
}

TEST_CASE("DimSpec::delta matches an explicit coordinate step", "[unit][space]") {
  const auto axes = box({6, 7, 8});
  const auto d = DimSpec::create(axes).value();
  const Direction dir = Direction::make({1, 2, -1, 0, 0, 0, 0, 0}, 3);
  const Coord from = Coord::of({2, 2, 3});
  const Coord to = Coord::of({3, 4, 2});
  REQUIRE(static_cast<std::int64_t>(d.toCell(from)) + d.delta(dir) ==
          static_cast<std::int64_t>(d.toCell(to)));
}
