// SPDX-License-Identifier: GPL-3.0-or-later
// The N-dimensional projection, tested where it is cheapest to test: as numbers.
//
// This is the same layout the Vulkan renderer consumes, so pinning it here means the
// renderer inherits behaviour that is already correct, and a layout regression shows
// up as a failing assertion rather than as a confusing picture.
#include "view/layout.hpp"

#include <map>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "support/geometry_helpers.hpp"

using namespace cb;
using namespace cb::view;

TEST_CASE("a 2-D board maps straight onto the screen axes", "[unit][view]") {
  const DimSpec d = test::makeDims({8, 8});
  const ViewConfig cfg = ViewConfig::forBoard(d);
  REQUIRE(cfg.screenAxes.size() == 2);
  REQUIRE(cfg.gridAxes.empty());

  const auto slices = enumerateSlices(d, cfg);
  REQUIRE(slices.size() == 1);

  const auto placed = layout(d, cfg);
  REQUIRE(placed.size() == 64);
  // Cell (3,5) sits at world (3,5,0): a 2-D board needs no projection at all.
  bool found = false;
  for (const Placement& p : placed) {
    if (p.cell != d.toCell(Coord::of({3, 5}))) continue;
    REQUIRE(p.x == 3.0f);
    REQUIRE(p.y == 5.0f);
    REQUIRE(p.z == 0.0f);
    found = true;
  }
  REQUIRE(found);
}

TEST_CASE("a 3-D board uses the third axis as depth", "[unit][view]") {
  const DimSpec d = test::makeDims({5, 5, 5});
  const ViewConfig cfg = ViewConfig::forBoard(d);
  REQUIRE(cfg.screenAxes.size() == 3);
  REQUIRE(cfg.gridAxes.empty());
  REQUIRE(enumerateSlices(d, cfg).size() == 1);
  REQUIRE(layout(d, cfg).size() == 125);
}

TEST_CASE("a 4-D board becomes a row of sub-boards", "[unit][view]") {
  const DimSpec d = test::makeDims({4, 4, 4, 4});
  const ViewConfig cfg = ViewConfig::forBoard(d);
  REQUIRE(cfg.screenAxes.size() == 3);
  REQUIRE(cfg.gridAxes.size() == 1);

  const auto slices = enumerateSlices(d, cfg);
  REQUIRE(slices.size() == 4);
  // Sub-boards are separated by the board's own extent plus the configured gap, so
  // they never overlap however the camera is placed.
  REQUIRE(slices[0].originX == 0.0f);
  REQUIRE(slices[1].originX == 4.0f + cfg.gridGap);
  REQUIRE(slices[3].originX == 3.0f * (4.0f + cfg.gridGap));
  REQUIRE(layout(d, cfg).size() == 256);
}

TEST_CASE("grid axes alternate between horizontal and vertical",
          "[unit][view]") {  // With two extra axes the sub-boards form a 2-D grid rather
                             // than one very long row.
  // This is the arrangement 5D chess uses for turn and timeline, and it is what keeps
  // a 5-axis board readable at all.
  const DimSpec d = test::makeDims({4, 4, 3, 3});
  ViewConfig cfg;
  cfg.screenAxes.push(0);
  cfg.screenAxes.push(1);
  cfg.gridAxes.push(2);
  cfg.gridAxes.push(3);
  REQUIRE(cfg.validate(d).has_value());

  const auto slices = enumerateSlices(d, cfg);
  REQUIRE(slices.size() == 9);
  const float pitch = 4.0f + cfg.gridGap;
  // First grid axis steps horizontally, second vertically.
  REQUIRE(slices[0].originX == 0.0f);
  REQUIRE(slices[0].originY == 0.0f);
  REQUIRE(slices[1].originX == pitch);
  REQUIRE(slices[1].originY == 0.0f);
  REQUIRE(slices[3].originX == 0.0f);
  REQUIRE(slices[3].originY == pitch);
  REQUIRE(slices[4].originX == pitch);
  REQUIRE(slices[4].originY == pitch);
}

TEST_CASE("a vertical grid stacks the sub-boards down the screen", "[unit][view]") {
  // The third screen axis of a 3-D board is depth, and from some cameras that reads as
  // boards piled straight up. Being able to ask for that on purpose - a column of
  // sub-boards rather than a row - is the other half of the axes control.
  const DimSpec d = test::makeDims({5, 5, 5});
  ViewConfig cfg;
  cfg.screenAxes.push(0);
  cfg.screenAxes.push(1);
  cfg.gridAxes.push(2);
  cfg.gridVertical = true;
  REQUIRE(cfg.validate(d).has_value());

  const auto slices = enumerateSlices(d, cfg);
  REQUIRE(slices.size() == 5);
  const float pitch = 5.0f + cfg.gridGap;
  REQUIRE(slices[0].originX == 0.0f);
  REQUIRE(slices[0].originY == 0.0f);
  REQUIRE(slices[1].originX == 0.0f);
  REQUIRE(slices[1].originY == pitch);  // down, not across
  // And the layout stays injective, so the boards cannot overlap.
  const auto placed = layout(d, cfg);
  std::set<std::tuple<float, float, float>> positions;
  for (const Placement& p : placed) REQUIRE(positions.insert({p.x, p.y, p.z}).second);
}

TEST_CASE("a vertical two-axis grid starts down the screen", "[unit][view]") {
  const DimSpec d = test::makeDims({4, 4, 3, 3});
  ViewConfig cfg;
  cfg.screenAxes.push(0);
  cfg.screenAxes.push(1);
  cfg.gridAxes.push(2);
  cfg.gridAxes.push(3);
  cfg.gridVertical = true;
  REQUIRE(cfg.validate(d).has_value());

  const auto slices = enumerateSlices(d, cfg);
  REQUIRE(slices.size() == 9);
  const float pitch = 4.0f + cfg.gridGap;
  REQUIRE(slices[1].originX == 0.0f);
  REQUIRE(slices[1].originY == pitch);  // first grid axis goes down
  REQUIRE(slices[3].originX == pitch);  // second goes across
  REQUIRE(slices[3].originY == 0.0f);
}

TEST_CASE("the layout is injective for every shipped board shape", "[unit][view]") {
  // Two cells sharing a world position would be both unreadable and unclickable, and
  // the failure would look like a rendering bug rather than a layout bug.
  const std::vector<std::vector<int>> shapes{
      {8, 8}, {5, 5, 5}, {4, 4, 4, 4}, {8, 8, 2}, {3, 3, 3, 3, 3}, {4, 4, 2, 2, 2, 2}};
  for (const auto& shape : shapes) {
    std::vector<AxisDecl> axes;
    for (std::size_t i = 0; i < shape.size(); ++i) {
      axes.push_back(AxisDecl{shape[i], AxisKind::Spatial, "a" + std::to_string(i)});
    }
    const DimSpec d = DimSpec::create(axes).value();
    const ViewConfig cfg = ViewConfig::forBoard(d);
    CAPTURE(shape.size(), d.cellCount());
    REQUIRE(cfg.validate(d).has_value());

    const auto placed = layout(d, cfg);
    REQUIRE(placed.size() == d.cellCount());

    std::set<std::tuple<float, float, float>> positions;
    std::set<CellId> cells;
    for (const Placement& p : placed) {
      REQUIRE(positions.insert({p.x, p.y, p.z}).second);  // no two cells coincide
      REQUIRE(cells.insert(p.cell).second);               // and every cell appears once
    }
    REQUIRE(cells.size() == d.cellCount());
  }
}

TEST_CASE("screen axes can be remapped without breaking injectivity", "[unit][view]") {
  const DimSpec d = test::makeDims({5, 5, 5});
  ViewConfig cfg;
  cfg.screenAxes.push(2);  // level drawn horizontally
  cfg.screenAxes.push(0);  // file drawn vertically
  cfg.gridAxes.push(1);    // rank becomes the grid
  REQUIRE(cfg.validate(d).has_value());

  const auto placed = layout(d, cfg);
  REQUIRE(placed.size() == 125);
  std::set<std::tuple<float, float, float>> positions;
  for (const Placement& p : placed) REQUIRE(positions.insert({p.x, p.y, p.z}).second);
  REQUIRE(enumerateSlices(d, cfg).size() == 5);
}

TEST_CASE("a view that leaves an axis out is rejected", "[unit][view]") {
  // An unassigned axis would stack its cells on top of each other, so this is a
  // validation error with a message that says exactly that.
  const DimSpec d = test::makeDims({4, 4, 4});
  ViewConfig cfg;
  cfg.screenAxes.push(0);
  cfg.screenAxes.push(1);
  const auto r = cfg.validate(d);
  REQUIRE_FALSE(r.has_value());
  REQUIRE(r.error().message.find("on top of each other") != std::string::npos);

  SECTION("a repeated axis is rejected too") {
    ViewConfig dup;
    dup.screenAxes.push(0);
    dup.screenAxes.push(0);
    dup.gridAxes.push(1);
    dup.gridAxes.push(2);
    REQUIRE_FALSE(dup.validate(d).has_value());
  }
  SECTION("an axis that does not exist is rejected") {
    ViewConfig bad;
    bad.screenAxes.push(0);
    bad.screenAxes.push(7);
    REQUIRE_FALSE(bad.validate(d).has_value());
  }
}

TEST_CASE("slice labels name their grid coordinates", "[unit][view]") {
  const DimSpec d = test::makeDims({4, 4, 4, 4});
  const ViewConfig cfg = ViewConfig::forBoard(d);
  const auto slices = enumerateSlices(d, cfg);
  REQUIRE(slices[2].label(d, cfg) == "a3=2");
}

TEST_CASE("bounds frame the whole scene", "[unit][view]") {
  const DimSpec d = test::makeDims({4, 4, 4, 4});
  const ViewConfig cfg = ViewConfig::forBoard(d);
  const Bounds b = boundsOf(layout(d, cfg));
  REQUIRE(b.minX == 0.0f);
  REQUIRE(b.maxX == 3.0f * (4.0f + cfg.gridGap) + 3.0f);
  // The third screen axis is spread out by depthSpacing so the levels stay legible.
  REQUIRE(b.maxZ == 3.0f * cfg.depthSpacing);
  REQUIRE(b.radius() > 0.0f);
}
