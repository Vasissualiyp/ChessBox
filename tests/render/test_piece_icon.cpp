// SPDX-License-Identifier: GPL-3.0-or-later
//
// The flat board's pieces are data, so the things that make an icon set usable can be
// checked rather than eyeballed: every archetype has one, nothing escapes its box, and
// the cheap set is actually cheap.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "assets/piece_model.hpp"
#include "render/piece_icon.hpp"
#include "render/ui_widgets.hpp"

using namespace cb;
using namespace cb::render;

namespace {

constexpr Archetype kPieces[] = {Archetype::Dome,  Archetype::Tower, Archetype::Wedge,
                                 Archetype::Spire, Archetype::Crown, Archetype::Monolith,
                                 Archetype::Horn};

/// Twice the signed area: zero means the outline has no interior to fill.
float doubleArea(IconPoly poly) {
  float a = 0;
  for (std::size_t i = 0; i < poly.size(); ++i) {
    const IconPoint& p = poly[i];
    const IconPoint& q = poly[(i + 1) % poly.size()];
    a += p.x * q.y - q.x * p.y;
  }
  return a;
}

}  // namespace

TEST_CASE("every piece archetype has an icon in both styles", "[render]") {
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    for (const Archetype a : kPieces) {
      const PieceIcon icon = pieceIcon(style, a);
      INFO(archetypeName(a) << " / " << iconStyleName(style));
      REQUIRE_FALSE(icon.fills.empty());
      for (const IconPoly& poly : icon.fills) {
        CHECK(poly.size() >= 3);
        CHECK(std::abs(doubleArea(poly)) > 1.0f);
      }
    }
  }
}

TEST_CASE("an archetype nobody anticipated still gets a piece", "[render]") {
  // A variant may declare something the renderer has never heard of. Drawing nothing
  // would make it unplayable in the flat view, which is worse than drawing a slab.
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    CHECK_FALSE(pieceIcon(style, static_cast<Archetype>(200)).fills.empty());
  }
  // The board's own cell is not a piece, and neither is a portal.
  CHECK(pieceIcon(IconStyle::Faceted, Archetype::Cell).fills.empty());
  CHECK(pieceIcon(IconStyle::Faceted, Archetype::Portal).fills.empty());
}

TEST_CASE("no icon escapes its box", "[render]") {
  // Everything is authored in a 100x100 box and scaled to the cell. A stray point is a
  // piece that overlaps its neighbours, which is exactly how the old set went wrong.
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    for (const Archetype a : kPieces) {
      const PieceIcon icon = pieceIcon(style, a);
      for (const IconPoly& poly : icon.fills) {
        for (const IconPoint& p : poly) {
          INFO(archetypeName(a) << " point " << p.x << "," << p.y);
          CHECK(p.x >= 0.0f);
          CHECK(p.x <= 100.0f);
          CHECK(p.y >= 0.0f);
          CHECK(p.y <= 100.0f);
        }
      }
    }
  }
}

TEST_CASE("the cheap set stays cheap", "[render]") {
  // The reason it exists at all. If it drifts towards the faceted set in complexity it
  // has stopped being an option and become a second default.
  for (const Archetype a : kPieces) {
    const PieceIcon icon = pieceIcon(IconStyle::Primitive, a);
    INFO(archetypeName(a));
    CHECK(icon.fills.size() <= 4);
    CHECK(icon.cuts.empty());
    std::size_t points = 0;
    for (const IconPoly& poly : icon.fills) points += poly.size();
    CHECK(points <= 24);
  }
}

TEST_CASE("every icon stands on the same line", "[render]") {
  // A set whose pieces sit at different heights reads as a set of unrelated drawings.
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    float foot = -1.0f;
    for (const Archetype a : kPieces) {
      float lowest = 0;
      for (const IconPoly& poly : pieceIcon(style, a).fills) {
        for (const IconPoint& p : poly) lowest = std::max(lowest, p.y);
      }
      if (foot < 0) foot = lowest;
      INFO(archetypeName(a) << " foot " << lowest << " expected " << foot);
      CHECK(std::abs(lowest - foot) < 0.01f);
    }
  }
}

TEST_CASE("a style name survives a round trip through the settings file", "[render]") {
  for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
    CHECK(iconStyleFromName(iconStyleName(style)) == style);
  }
  // An unrecognised name is the default, not an error: a settings file from a newer
  // build must still load.
  CHECK(iconStyleFromName("nonsense") == IconStyle::Faceted);
}

// ---------------------------------------------------------------------------
// Time's arrow. A grid of boards does not say which way the game runs through it, and on
// a board with a turn axis that is the first thing a player has to know.
// ---------------------------------------------------------------------------
#ifdef CB_HAVE_VULKAN
#include "render/board_renderer.hpp"
#include "support/variants.hpp"
#include "view/snapshot.hpp"

TEST_CASE("a temporal board draws a rail under each timeline", "[render]") {
  using namespace cb;
  BoardRenderer renderer;

  const VariantSpec flat = test::loadVariant("standard");
  const Position fp = Position::startPosition(flat);
  const InstanceSet plain = renderer.buildInstances(
      view::PositionView::capture(fp), view::ViewConfig::forBoard(flat.dims));
  // A board with no turn axis has no arrow to draw at all.
  CHECK(plain.batches[static_cast<std::size_t>(Archetype::Arrow)].count == 0);

  const VariantSpec five = test::loadVariant("5d");
  const Position sp = Position::startPosition(five);
  const InstanceSet temporal = renderer.buildInstances(
      view::PositionView::capture(sp), view::ViewConfig::forBoard(five.dims));
  // Every timeline gets a head, and the head is its own shape rather than a chess
  // piece's wedge - so this cannot pass just because a variant happens to field knights.
  CHECK(temporal.batches[static_cast<std::size_t>(Archetype::Arrow)].count > 0);
}

TEST_CASE("a valley in an outline stays a valley", "[render]") {
  // The bug this exists for: a notch cut into a piece's outline came back filled, so the
  // notch disappeared. A fill that triangulates properly conserves the polygon's area;
  // one that treats the shape as convex invents the area of the notch.
  const auto area = [](const std::vector<ImVec2>& p) {
    float a = 0.0f;
    for (std::size_t i = 0; i < p.size(); ++i) {
      const ImVec2& u = p[i];
      const ImVec2& v = p[(i + 1) % p.size()];
      a += u.x * v.y - v.x * u.y;
    }
    return std::abs(a) * 0.5f;
  };
  const auto triArea = [](const std::vector<ImVec2>& p, const std::vector<int>& idx) {
    float a = 0.0f;
    for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
      const ImVec2& u = p[static_cast<std::size_t>(idx[i])];
      const ImVec2& v = p[static_cast<std::size_t>(idx[i + 1])];
      const ImVec2& w = p[static_cast<std::size_t>(idx[i + 2])];
      a += std::abs((v.x - u.x) * (w.y - u.y) - (w.x - u.x) * (v.y - u.y)) * 0.5f;
    }
    return a;
  };

  SECTION("a square notch cut into the top of a block") {
    const std::vector<ImVec2> p{{0, 100}, {100, 100}, {100, 0}, {70, 0},
                                {70, 60}, {30, 60},   {30, 0},  {0, 0}};
    std::vector<int> idx;
    widgets::triangulate(p.data(), static_cast<int>(p.size()), idx);
    REQUIRE(idx.size() == (p.size() - 2) * 3);
    CHECK(std::abs(triArea(p, idx) - area(p)) < 1.0f);
    // And the notch really is missing area: a convex reading would give the full block.
    CHECK(area(p) < 100.0f * 100.0f - 100.0f);
  }

  SECTION("wound the other way round, it fills the same") {
    std::vector<ImVec2> p{{0, 100}, {100, 100}, {100, 0}, {70, 0},
                          {70, 60}, {30, 60},   {30, 0},  {0, 0}};
    std::reverse(p.begin(), p.end());
    std::vector<int> idx;
    widgets::triangulate(p.data(), static_cast<int>(p.size()), idx);
    REQUIRE(idx.size() == (p.size() - 2) * 3);
    CHECK(std::abs(triArea(p, idx) - area(p)) < 1.0f);
  }

  SECTION("every shipped icon outline triangulates") {
    for (const IconStyle style : {IconStyle::Faceted, IconStyle::Primitive}) {
      for (int a = 1; a <= 7; ++a) {
        const PieceIcon icon = pieceIcon(style, static_cast<Archetype>(a));
        for (const IconPoly& poly : icon.fills) {
          std::vector<ImVec2> pts;
          for (const IconPoint& q : poly) pts.push_back(ImVec2(q.x, q.y));
          std::vector<int> idx;
          widgets::triangulate(pts.data(), static_cast<int>(pts.size()), idx);
          INFO("archetype " << a << " with " << pts.size() << " points");
          CHECK(idx.size() == (pts.size() - 2) * 3);
        }
      }
    }
  }

  SECTION("an outline that is not a shape is caught before it is drawn") {
    // Not by the filler - ear clipping fills, it does not validate, and on four points
    // it will happily halve a bow tie. Whether an outline crosses itself is a question
    // about the *model*, and the designers ask it there before an author can save.
    assets::IconModel bad;
    bad.fills.push_back(assets::Outline{{{0, 0}, {400, 0}, {0, 400}, {400, 400}}});
    CHECK_FALSE(assets::validate(bad).has_value());
  }
}

#endif
