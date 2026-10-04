// SPDX-License-Identifier: GPL-3.0-or-later
//
// The file/rank coordinate labels, and where they are allowed to appear (M17.21 label
// revision).
//
// They are text projected through the camera, and they read badly in any 3-D view: a
// letter skews, overlaps its neighbour, or goes nearly edge-on. The decision to show them
// is one predicate, so it is pinned here rather than left implicit inside `buildGameHud`.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI

#include "app/session.hpp"
#include "render/ui.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::render;

TEST_CASE("coordinate labels are drawn only in the flat 2-D view", "[render]") {
  app::Settings settings;
  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session.has_value());
  app::Session& s = **session;

  // The one place they belong: flat, orthographic, two dimensions.
  settings.showCoordinates = true;
  s.setFlatView(true);
  CHECK(coordinateLabelsShown(settings, s));

  // Off the flat 2-D projection - the ordinary solid board and the shape view alike -
  // the camera-projected text is dropped.
  s.setFlatView(false);
  CHECK_FALSE(coordinateLabelsShown(settings, s));

  // The player's toggle still owns them.
  s.setFlatView(true);
  settings.showCoordinates = false;
  CHECK_FALSE(coordinateLabelsShown(settings, s));

  // And they are 2-D only, however flat the view.
  settings.showCoordinates = true;
  auto cube = app::Session::create(test::loadVariant("cube5"));
  REQUIRE(cube.has_value());
  (**cube).setFlatView(true);
  CHECK_FALSE(coordinateLabelsShown(settings, **cube));
}

#endif  // CB_HAVE_IMGUI
