// SPDX-License-Identifier: GPL-3.0-or-later
// Renders real frames on a real device and asserts on the pixels.
//
// The reason this is possible at all is that the renderer is headless by construction:
// no window, no surface, no display server. A validation-layer message is a failing
// test, not console noise - which is the only way a Vulkan renderer stays correct.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>

#include "app/session.hpp"
#include "io/fen.hpp"
#include "render/board_renderer.hpp"
#include "render/offscreen_target.hpp"
#include "render/vulkan_context.hpp"
#include "support/variants.hpp"
#include "view/theme.hpp"

using namespace cb;
using namespace cb::render;

namespace {

/// A context shared by the tests in this file: creating one per test would multiply the
/// (slow) device setup for no benefit, and validation state is checked per render.
struct Gpu {
  VulkanContext ctx;
  bool available{false};
  std::string reason;

  Gpu() {
    auto c = VulkanContext::create();
    if (c.has_value()) {
      ctx = std::move(*c);
      available = true;
    } else {
      reason = c.error().format();
    }
  }
};

Gpu& gpu() {
  static Gpu g;
  return g;
}

/// Where debug captures land, so a human can look at what a failing test saw.
std::filesystem::path capturePath(const std::string& name) {
  const std::filesystem::path dir =
      std::filesystem::path(CB_BINARY_DIR) / "render-captures";
  std::filesystem::create_directories(dir);
  return dir / (name + ".ppm");
}

struct Frame {
  Image image;
  std::size_t validationErrors{0};
  std::vector<std::string> messages;
};

Frame renderVariant(const std::string& name, std::uint32_t w = 512, std::uint32_t h = 384,
                    bool highlight = false) {
  const VariantSpec& v = *new VariantSpec(test::loadVariant(name));  // outlives the frame
  Position p = Position::startPosition(v);
  view::PositionView snap = view::PositionView::capture(p);
  if (highlight) {
    snap.setSelected(0);
    snap.setHighlighted({1, 2, 3});
  }

  auto target = OffscreenTarget::create(gpu().ctx, w, h);
  REQUIRE(target.has_value());
  auto renderer = BoardRenderer::create(gpu().ctx);
  REQUIRE(renderer.has_value());

  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::OrbitCamera cam =
      view::OrbitCamera::frame(view::boundsOf(view::layout(v.dims, cfg)));

  (void)gpu().ctx.takeValidationMessages();  // start from a clean slate
  auto img = renderer->renderToImage(*target, snap, cfg, cam);
  REQUIRE(img.has_value());

  Frame f;
  f.image = std::move(*img);
  f.validationErrors = gpu().ctx.validationErrorCount();
  f.messages = gpu().ctx.takeValidationMessages();
  return f;
}

}  // namespace

TEST_CASE("a Vulkan device is available and validation is on", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  REQUIRE_FALSE(gpu().ctx.deviceName().empty());
  WARN("rendering on: " << gpu().ctx.deviceName()
                        << (gpu().ctx.validationEnabled() ? " (validation on)"
                                                          : " (validation UNAVAILABLE)"));
  REQUIRE(gpu().ctx.device() != VK_NULL_HANDLE);
}

TEST_CASE("instances are built from a snapshot without a GPU", "[render]") {
  // Pure data in, pure data out - so the part of the renderer most likely to be wrong is
  // testable with no device at all.
  const VariantSpec v = test::loadVariant("standard");
  const Position p = Position::startPosition(v);
  view::PositionView snap = view::PositionView::capture(p);

  BoardRenderer renderer;  // no device needed for buildInstances
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const InstanceSet set = renderer.buildInstances(snap, cfg);

  // 64 cells, 32 pieces, and the plinth the board stands on - which is drawn with the
  // same slab mesh as a cell, so it lands in the cell batch.
  REQUIRE(set.size() == 64 + 32 + 1);
  REQUIRE(set.batches[static_cast<std::size_t>(Archetype::Cell)].count == 64 + 1);
  std::uint32_t counted = 0;
  for (const auto& b : set.batches) counted += b.count;
  REQUIRE(counted == set.size());

  SECTION("highlighting recolours exactly the cells the engine named") {
    snap.setSelected(v.dims.toCell(Coord::of({4, 1})));
    snap.setHighlighted(
        {v.dims.toCell(Coord::of({4, 2})), v.dims.toCell(Coord::of({4, 3}))});
    const InstanceSet lit = renderer.buildInstances(snap, cfg);
    REQUIRE(lit.size() == set.size());

    // The selected cell takes the accent outright; the destinations are *tinted*
    // toward the legal-move colour so the checkerboard still reads underneath.
    int changed = 0;
    for (std::size_t i = 0; i < lit.instances.size(); ++i) {
      const bool differs =
          std::abs(lit.instances[i].color[0] - set.instances[i].color[0]) > 0.02f ||
          std::abs(lit.instances[i].color[1] - set.instances[i].color[1]) > 0.02f;
      if (differs) ++changed;
    }
    REQUIRE(changed == 3);

    const auto& ember = renderer.theme().ember;
    int selected = 0;
    for (const Instance& inst : lit.instances) {
      if (std::abs(inst.color[0] - ember.r) < 0.01f &&
          std::abs(inst.color[1] - ember.g) < 0.01f) {
        ++selected;
      }
    }
    REQUIRE(selected == 1);
  }
}

TEST_CASE("a glued board marks each seam with the colour of where it leads", "[render]") {
  // One cyan rail per edge is not enough information: a cylinder and a Moebius band
  // draw the identical picture, and the difference between them is the whole game. So
  // every glued face gets its own rail, coloured by the portal it belongs to, and the
  // two ends of one portal match.
  BoardRenderer renderer;
  const view::Theme theme = renderer.theme();

  const VariantSpec box = test::loadVariant("standard");
  const Position bp = Position::startPosition(box);
  const view::ViewConfig boxCfg = view::ViewConfig::forBoard(box.dims);
  const view::SeamMap boxSeams = view::SeamMap::build(box, boxCfg, theme);
  const InstanceSet plain =
      renderer.buildInstances(view::PositionView::capture(bp), boxCfg, &boxSeams);
  CHECK(boxSeams.empty());

  const VariantSpec torus = test::loadVariant("torus");
  const Position tp = Position::startPosition(torus);
  const view::ViewConfig cfg = view::ViewConfig::forBoard(torus.dims);
  const view::SeamMap seams = view::SeamMap::build(torus, cfg, theme);
  const InstanceSet glued =
      renderer.buildInstances(view::PositionView::capture(tp), cfg, &seams);

  // Both axes are glued, so every cell on the border carries a rail, and the four
  // corners carry two.
  CHECK(seams.faces().size() == 8 * 4);
  CHECK(glued.size() == plain.size() + seams.faces().size());

  // Switching the marks off leaves the board alone.
  BoardOptions quiet = renderer.options();
  quiet.showSeams = false;
  renderer.setOptions(quiet);
  const InstanceSet bare =
      renderer.buildInstances(view::PositionView::capture(tp), cfg, &seams);
  CHECK(bare.size() == plain.size());
}

TEST_CASE("a portal is a doorway standing across its seam", "[render]") {
  // The portal used to be drawn with the flat cell slab, which lies on the board however
  // it is scaled - so a portal on a vertical file seam came out horizontal. It is a cube
  // now, scaled thin on the axis it faces.
  const VariantSpec v = test::loadVariant("cylinder");
  const view::ViewConfig cfg = view::ViewConfig::forBoard(v.dims);
  const view::Theme theme = BoardRenderer{}.theme();
  const view::SeamMap seams = view::SeamMap::build(v, cfg, theme);

  auto pos = fromFen(v, "8/8/8/8/8/8/8/R7 w - - 0 1");
  REQUIRE(pos.has_value());
  const PieceTypeId rook = v.findPiece("rook");
  Move m;
  m.from = v.dims.toCell(Coord::of({0, 0}));
  m.to = v.dims.toCell(Coord::of({6, 0}));
  const view::MovePath path = view::tracePath(v, *pos, rook, Color::White, m);

  view::MoveAnimation anim;
  anim.start(cfg, view::layout(v.dims, cfg), seams, theme, path, 0.12f);
  for (int i = 0; i < 500 && anim.active() && anim.openPortals().empty(); ++i) {
    anim.advance(0.005f);
  }
  REQUIRE_FALSE(anim.openPortals().empty());

  const InstanceSet set = BoardRenderer{}.buildInstances(
      view::PositionView::capture(*pos), cfg, &seams, &anim);
  const auto& batch = set.batches[static_cast<std::size_t>(Archetype::Portal)];
  REQUIRE(batch.count > 0);
  for (std::uint32_t i = 0; i < batch.count; ++i) {
    const Instance& iris = set.instances[batch.first + i];
    // Thin across the file seam (X), wide along it (Y), tall (Z): a vertical doorway.
    CHECK(iris.scale[0] < iris.scale[1]);
    CHECK(iris.scale[0] < iris.scale[2]);
  }
}

TEST_CASE("pieces get a shape from how they move", "[render]") {
  // A variant can declare a piece nobody anticipated, so a shape has to be derivable.
  const VariantSpec v = test::loadVariant("standard");
  REQUIRE(archetypeFor(v.pieces[v.findPiece("pawn")]) == Archetype::Dome);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("king")]) == Archetype::Monolith);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("rook")]) == Archetype::Tower);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("bishop")]) == Archetype::Spire);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("queen")]) == Archetype::Crown);
  REQUIRE(archetypeFor(v.pieces[v.findPiece("knight")]) == Archetype::Wedge);

  // A piece that needs three axes at once only exists above two dimensions, and gets a
  // shape that says so.
  const VariantSpec cube = test::loadVariant("cube5");
  REQUIRE(archetypeFor(cube.pieces[cube.findPiece("unicorn")]) == Archetype::Horn);

  // Height encodes value, so an unfamiliar army still reads at a glance.
  REQUIRE(heightFor(v.pieces[v.findPiece("king")]) >
          heightFor(v.pieces[v.findPiece("pawn")]));
  REQUIRE(heightFor(v.pieces[v.findPiece("queen")]) >
          heightFor(v.pieces[v.findPiece("pawn")]));

  // An explicit declaration always wins over the inference.
  PieceTypeDef custom;
  custom.name = "thing";
  custom.shape = "horn";
  custom.heightPermille = 1500;
  REQUIRE(archetypeFor(custom) == Archetype::Horn);
  REQUIRE(heightFor(custom) == 1.5f);
  // And an unknown name falls back rather than failing - a variant is never unplayable
  // for want of a model.
  custom.shape = "tesseract";
  REQUIRE(archetypeFor(custom) == Archetype::Tower);
}

TEST_CASE("every archetype produces a usable mesh", "[render]") {
  const MeshLibrary lib = MeshLibrary::build();
  REQUIRE_FALSE(lib.vertices.empty());
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    CAPTURE(archetypeName(static_cast<Archetype>(i)));
    const MeshRange& r = lib.ranges[i];
    REQUIRE(r.indexCount > 0);
    REQUIRE(r.indexCount % 3 == 0);
    // Indices are relative to the range's vertex offset, so every one must land inside
    // that archetype's own vertices - a stray index would draw another piece's geometry.
    std::uint16_t maxIndex = 0;
    for (std::uint32_t k = 0; k < r.indexCount; ++k) {
      maxIndex = std::max(maxIndex, lib.indices[r.firstIndex + k]);
    }
    REQUIRE(static_cast<std::size_t>(r.vertexOffset) + maxIndex < lib.vertices.size());
  }
}

TEST_CASE("a 2-D board renders with no validation errors", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const Frame f = renderVariant("standard");
  CAPTURE(f.messages);
  REQUIRE(f.validationErrors == 0);

  REQUIRE(f.image.width == 512);
  REQUIRE(f.image.height == 384);
  REQUIRE(f.image.rgba.size() == 512u * 384u * 4u);

  // Something was actually drawn, and it is not a solid fill. The ground comes from the
  // theme rather than being written out here: a retheme is allowed, a board that stops
  // being drawn is not.
  const view::Theme theme = BoardRenderer{}.theme();
  const auto byte = [](float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
  };
  const Image::Rgba background{byte(theme.ink.r), byte(theme.ink.g), byte(theme.ink.b),
                               255};
  const int coverage = f.image.coveragePerMille(background);
  CAPTURE(coverage);
  REQUIRE(coverage > 200);
  REQUIRE(coverage < 1000);
  // Shading over two cell colours and two piece colours gives a wide palette; a single
  // flat colour would mean the instance data never reached the shader.
  REQUIRE(f.image.distinctColors() > 8);
  // The camera frames the board with margin, so the corners stay background.
  REQUIRE(f.image.at(0, 0) == background);
  REQUIRE(f.image.at(511, 0) == background);

  REQUIRE(writePpm(f.image, capturePath("standard")).has_value());
}

TEST_CASE("rendering is deterministic", "[render][gpu]") {
  // Two identical frames must be bit-identical. Without this, an image assertion is
  // untrustworthy and every later golden would be flaky.
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const Frame a = renderVariant("standard");
  const Frame b = renderVariant("standard");
  REQUIRE(a.image.rgba == b.image.rgba);
}

TEST_CASE("highlighting changes the picture", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  const Frame plain = renderVariant("standard", 512, 384, /*highlight=*/false);
  const Frame lit = renderVariant("standard", 512, 384, /*highlight=*/true);
  REQUIRE(plain.image.rgba != lit.image.rgba);
  REQUIRE(lit.validationErrors == 0);
}

TEST_CASE("3-D and 4-D boards render, and look different from each other",
          "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  // The theme's ground: a brown-biased near-black, not a neutral grey.
  const Image::Rgba background{13, 11, 10, 255};

  const Frame cube = renderVariant("cube5");
  CAPTURE(cube.messages);
  REQUIRE(cube.validationErrors == 0);
  REQUIRE(cube.image.coveragePerMille(background) > 150);
  REQUIRE(writePpm(cube.image, capturePath("cube5")).has_value());

  const Frame hyper = renderVariant("hyper4");
  CAPTURE(hyper.messages);
  REQUIRE(hyper.validationErrors == 0);
  // A 4-D board is drawn as a row of sub-boards, so it covers the frame differently.
  REQUIRE(hyper.image.coveragePerMille(background) > 100);
  REQUIRE(writePpm(hyper.image, capturePath("hyper4")).has_value());

  REQUIRE(cube.image.rgba != hyper.image.rgba);
}

TEST_CASE("every shipped variant renders cleanly", "[render][gpu]") {
  if (!gpu().available) SKIP("no Vulkan device: " + gpu().reason);
  for (const char* name : {"standard", "cylinder", "torus", "mobius", "klein",
                           "mirrorbox", "cube5", "hyper4", "torus3d"}) {
    CAPTURE(name);
    const Frame f = renderVariant(name, 256, 192);
    CAPTURE(f.messages);
    REQUIRE(f.validationErrors == 0);
    REQUIRE(f.image.coveragePerMille(Image::Rgba{13, 11, 10, 255}) > 50);
    REQUIRE(writePpm(f.image, capturePath(name)).has_value());
  }
}

TEST_CASE("a branch connector turns into its rail on a rounded corner", "[render]") {
  // The corner of a timeline elbow used to be two rectangles overlapping at a right
  // angle. A quarter-round fillet is placed in the empty inside of the bend, turned by
  // the instance's roll into the right quadrant. Built with no GPU: it is instance data.
  auto session = app::Session::create(test::loadVariant("5d"));
  REQUIRE(session.has_value());
  app::Session& s = **session;
  const auto click = [&](CellId c) {
    app::Action a;
    a.kind = app::ActionKind::ClickCell;
    a.cell = c;
    REQUIRE(s.apply(a).has_value());
  };
  const auto boardOf = [&](CellId c) {
    const Coord co = s.variant().dims.toCoord(c);
    return std::pair<int, int>{co.c[2], co.c[3]};
  };
  const auto playFirst = [&](bool normal) {
    Move chosen{};
    bool found = false;
    for (const Move& m : s.game().legalMoves()) {
      if ((boardOf(m.from) == boardOf(m.to)) == normal) {
        chosen = m;
        found = true;
        break;
      }
    }
    REQUIRE(found);
    click(chosen.from);
    click(chosen.to);
  };
  playFirst(true);   // White advances.
  playFirst(true);   // Black advances.
  playFirst(false);  // Black travels into the past and branches.
  REQUIRE_FALSE(s.timelineLinks().empty());

  BoardRenderer renderer;
  const view::ViewConfig cfg = view::ViewConfig::forBoard(s.variant().dims);
  const InstanceSet set = renderer.buildInstances(
      s.snapshot(), cfg, nullptr, nullptr, [&](CellId c) { return s.boardVisible(c); },
      {}, s.timelineLinks());
  const auto& batch = set.batches[static_cast<std::size_t>(Archetype::Fillet)];
  REQUIRE(batch.count >= 1);
  const Instance& fil = set.instances[batch.first];
  // A quarter-turn multiple of the roll, and the fillet as thick as the rail it sits on.
  const float quarter = fil.roll / (0.5f * std::numbers::pi_v<float>);
  CHECK(std::abs(quarter - std::round(quarter)) < 0.01f);
  CHECK(std::abs(fil.scale[2] - 0.025f / 0.055f) < 0.05f);
}

#endif  // CB_HAVE_VULKAN
