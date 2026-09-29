// SPDX-License-Identifier: GPL-3.0-or-later
//
// A palette is not a matter of taste alone: some of it is load-bearing. These pin the
// parts that are, so a later retheme cannot quietly make a piece invisible.
#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "view/theme.hpp"

using namespace cb::view;

namespace {

/// Relative luminance, WCAG's definition - the one contrast ratios are built on.
float luminance(const Rgba& c) {
  const auto lin = [](float v) {
    return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
  };
  return 0.2126f * lin(c.r) + 0.7152f * lin(c.g) + 0.0722f * lin(c.b);
}

float contrast(const Rgba& a, const Rgba& b) {
  const float la = luminance(a), lb = luminance(b);
  const float hi = std::max(la, lb), lo = std::min(la, lb);
  return (hi + 0.05f) / (lo + 0.05f);
}

}  // namespace

TEST_CASE("both piece colours read on both squares", "[unit][view]") {
  // This is the constraint that set the two board values, in both themes. A piece the
  // player cannot pick out of the square it stands on is not a style choice.
  for (const Theme& t : {Theme::manifold(), Theme::console()}) {
    CHECK(contrast(t.whitePiece, t.boardLight) > 1.8f);
    CHECK(contrast(t.whitePiece, t.boardDark) > 1.8f);
    CHECK(contrast(t.blackPiece, t.boardLight) > 1.8f);
    CHECK(contrast(t.blackPiece, t.boardDark) > 1.8f);
  }
}

TEST_CASE("body text is legible on the ground it is drawn on", "[unit][view]") {
  for (const Theme& t : {Theme::manifold(), Theme::console()}) {
    CHECK(contrast(t.bone, t.ink) > 7.0f);      // primary type
    CHECK(contrast(t.boneDim, t.panel) > 4.5f); // secondary type on a plate
    CHECK(contrast(t.ember, t.ink) > 3.0f);     // the accent, against the ground
  }
}

TEST_CASE("the shell theme is light and its board is darker than the page",
          "[unit][view]") {
  const Theme t = Theme::manifold();
  CHECK(t.light);
  // The board is the focus: both its squares sit below the page it is drawn on.
  CHECK(luminance(t.boardLight) < luminance(t.ink));
  CHECK(luminance(t.boardDark) < luminance(t.boardLight));
  CHECK(luminance(t.boardRim) < luminance(t.boardDark));
}

TEST_CASE("the candlelit theme is still dark, and unchanged", "[unit][view]") {
  const Theme t = Theme::console();
  CHECK_FALSE(t.light);
  CHECK(luminance(t.ink) < 0.02f);
  CHECK(luminance(t.bone) > luminance(t.ink));
}
