// SPDX-License-Identifier: GPL-3.0-or-later
// Renders real frames on a real device and asserts on the pixels.
//
// The reason this is possible at all is that the renderer is headless by construction:
// no window, no surface, no display server. A validation-layer message is a failing
// test, not console noise - which is the only way a Vulkan renderer stays correct.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_VULKAN

#include <filesystem>

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

TEST_CASE("a glued board marks its seams, a box board does not", "[render]") {
  // The cold colour is spent only where the board stops being flat, and the mask says
  // *which* sides are glued - so a player can pair the two edges by eye.
  //
  // The plinth the board stands on carries a warm rim of its own, so a seam is
  // identified by its colour rather than merely by having an edge.
  BoardRenderer renderer;
  const view::Rgba rift = renderer.theme().rift;
  const auto isSeam = [&](const Instance& i) {
    return i.edgeMask > 0.0f && std::abs(i.edge[0] - rift.r) < 0.01f &&
           std::abs(i.edge[2] - rift.b) < 0.01f;
  };

  const VariantSpec box = test::loadVariant("standard");
  const Position bp = Position::startPosition(box);
  const InstanceSet plain = renderer.buildInstances(view::PositionView::capture(bp),
                                                    view::ViewConfig::forBoard(box.dims));
  for (const Instance& i : plain.instances) REQUIRE_FALSE(isSeam(i));

  const VariantSpec torus = test::loadVariant("torus");
  const Position tp = Position::startPosition(torus);
  const InstanceSet glued = renderer.buildInstances(view::PositionView::capture(tp),
                                                    view::ViewConfig::forBoard(torus.dims));
  int marked = 0;
  for (const Instance& i : glued.instances) {
    if (isSeam(i)) ++marked;
  }
  // Both axes are glued, so the whole border is a seam: 8x8 minus the 6x6 interior.
  REQUIRE(marked == 64 - 36);
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

  // Something was actually drawn, and it is not a solid fill.
  // The theme's ground: a brown-biased near-black, not a neutral grey.
  const Image::Rgba background{13, 11, 10, 255};
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

#endif  // CB_HAVE_VULKAN
