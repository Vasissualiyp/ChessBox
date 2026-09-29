// SPDX-License-Identifier: GPL-3.0-or-later
//
// The flat board's pieces are data, so the things that make an icon set usable can be
// checked rather than eyeballed: every archetype has one, nothing escapes its box, and
// the cheap set is actually cheap.
#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "render/piece_icon.hpp"

using namespace cb::render;

namespace {

constexpr Archetype kPieces[] = {Archetype::Dome,  Archetype::Tower,
                                 Archetype::Wedge, Archetype::Spire,
                                 Archetype::Crown, Archetype::Monolith,
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
