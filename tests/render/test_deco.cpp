// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shell's screen change is a camera move, and these are the two properties of it
// that are worth pinning: a decoration at alpha 0 must leave no geometry behind, and
// zooming it must scale it rather than move it. Both are assertions about a draw list,
// so no GPU is involved.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI

#include <utility>
#include <vector>

#include <imgui.h>

#include "render/deco.hpp"

using namespace cb;
using namespace cb::render;

namespace {

/// A draw list with an ImGui context behind it, so `drawDeco` can be run headlessly.
struct DecoFrame {
  ImGuiContext* ctx{nullptr};
  ImDrawList dl;
  DecoFrame() : ctx(ImGui::CreateContext()), dl(ImGui::GetDrawListSharedData()) {
    dl._ResetForNewFrame();
  }
  ~DecoFrame() {
    if (ctx != nullptr) ImGui::DestroyContext(ctx);
  }
};

std::pair<float, float> extent(const ImDrawList& dl) {
  if (dl.VtxBuffer.Size == 0) return {0.0f, 0.0f};
  float minX = dl.VtxBuffer[0].pos.x;
  float maxX = minX;
  float minY = dl.VtxBuffer[0].pos.y;
  float maxY = minY;
  for (int i = 1; i < dl.VtxBuffer.Size; ++i) {
    minX = std::min(minX, dl.VtxBuffer[i].pos.x);
    maxX = std::max(maxX, dl.VtxBuffer[i].pos.x);
    minY = std::min(minY, dl.VtxBuffer[i].pos.y);
    maxY = std::max(maxY, dl.VtxBuffer[i].pos.y);
  }
  return {maxX - minX, maxY - minY};
}

}  // namespace

TEST_CASE("a decoration at alpha zero emits nothing", "[render]") {
  DecoFrame f;
  drawDeco(&f.dl, Deco::Manifold, ImVec2(0, 0), ImVec2(400, 300), view::Theme::manifold(),
           IconStyle::Faceted, 0.0f, nullptr, 1.0f, 0.0f);
  CHECK(f.dl.VtxBuffer.Size == 0);

  // And a visible one does emit something, so the test above is not vacuous.
  DecoFrame g;
  drawDeco(&g.dl, Deco::Manifold, ImVec2(0, 0), ImVec2(400, 300), view::Theme::manifold(),
           IconStyle::Faceted, 0.0f, nullptr, 1.0f, 1.0f);
  CHECK(g.dl.VtxBuffer.Size > 0);
}

TEST_CASE("the candlelit theme's drifting field is muted and dark", "[render]") {
  // The drifting polygons are the only colours in the shell that mean nothing, but they
  // still have to follow the page: on the light theme they are pastel, and on the
  // candlelit theme they must sit under the interface rather than glow through it. Pinned
  // as "the console field is darker than the manifold field" rather than as exact
  // colours, so the palettes can be tuned without rewriting the test.
  DepthField field;
  field.advance(1.0f);
  const auto meanLuminance = [&](const view::Theme& th) {
    DecoFrame f;
    field.draw(&f.dl, ImVec2(0, 0), ImVec2(400, 300), th, IconStyle::Faceted, true, true);
    float sum = 0.0f;
    int n = 0;
    for (int i = 0; i < f.dl.VtxBuffer.Size; ++i) {
      const ImU32 c = f.dl.VtxBuffer[i].col;
      const float r = static_cast<float>(c & 0xFF) / 255.0f;
      const float g = static_cast<float>((c >> 8) & 0xFF) / 255.0f;
      const float b = static_cast<float>((c >> 16) & 0xFF) / 255.0f;
      sum += 0.2126f * r + 0.7152f * g + 0.0722f * b;
      ++n;
    }
    return n == 0 ? 0.0f : sum / static_cast<float>(n);
  };
  const float light = meanLuminance(view::Theme::manifold());
  const float dark = meanLuminance(view::Theme::console());
  REQUIRE(light > 0.0f);
  CHECK(dark < light);
  CHECK(dark < 0.35f);  // muted, not merely a little darker
}

TEST_CASE("each wireframe body is a real drawing", "[render]") {
  // The field's vocabulary is five geometries, and each has to project to an actual
  // figure rather than an empty path or a degenerate line: a shape function that divides
  // by a radius or forgets an angle can silently emit nothing, and a background of
  // nothing looks exactly like a feature that was never wired up.
  const int count = static_cast<int>(WireShape::Count);
  REQUIRE(count == 5);
  for (int s = 0; s < count; ++s) {
    for (const float t : {0.0f, 0.5f, 2.0f}) {
      DecoFrame f;
      drawWireShape(&f.dl, static_cast<WireShape>(s), ImVec2(200.0f, 150.0f), 80.0f, t,
                    t * 0.5f, view::Theme::manifold(), 0.3f, 0.4f);
      REQUIRE(f.dl.VtxBuffer.Size > 0);
      const auto [w, h] = extent(f.dl);
      CHECK(w > 10.0f);
      CHECK(h > 10.0f);
    }
  }
}

TEST_CASE("the drifting field is the same picture from the same state", "[render]") {
  // The shell draws the same frame twice in a headless capture, so the field has to be a
  // pure function of its own accumulated state. Two fields advanced by the same clock must
  // emit byte-identical geometry; a colour or position that reaches for a global random
  // source shows up here as a mismatch.
  DepthField a;
  DepthField b;
  for (int i = 0; i < 5; ++i) {
    a.advance(0.016f);
    b.advance(0.016f);
  }
  DecoFrame fa;
  DecoFrame fb;
  a.draw(&fa.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
         IconStyle::Faceted, true, true);
  b.draw(&fb.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
         IconStyle::Faceted, true, true);
  REQUIRE(fa.dl.VtxBuffer.Size > 0);
  REQUIRE(fa.dl.VtxBuffer.Size == fb.dl.VtxBuffer.Size);
  for (int i = 0; i < fa.dl.VtxBuffer.Size; ++i) {
    CHECK(fa.dl.VtxBuffer[i].pos.x == fb.dl.VtxBuffer[i].pos.x);
    CHECK(fa.dl.VtxBuffer[i].pos.y == fb.dl.VtxBuffer[i].pos.y);
    CHECK(fa.dl.VtxBuffer[i].col == fb.dl.VtxBuffer[i].col);
  }
}

TEST_CASE("the board screen's field is wireframes and no pieces", "[render]") {
  // Behind a real game there must be no piece icons: the board already has actual pieces,
  // and a second set drifting behind them competes with the thing being played. The quiet
  // field is therefore wireframes only, whatever flags its caller passes.
  DepthField quiet{true};
  quiet.advance(1.0f);
  DecoFrame wires;
  quiet.draw(&wires.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
             IconStyle::Faceted, false, true);
  CHECK(wires.dl.VtxBuffer.Size > 0);
  DecoFrame pieces;
  quiet.draw(&pieces.dl, ImVec2(0, 0), ImVec2(640, 360), view::Theme::manifold(),
             IconStyle::Faceted, true, false);
  CHECK(pieces.dl.VtxBuffer.Size == 0);
}

TEST_CASE("a decoration answers a turn", "[render]") {
  // The menu objects can be dragged. The failure this guards against is the quiet one:
  // a turn parameter that is threaded through the call but never reaches the camera,
  // which looks exactly like an object that simply does not rotate.
  const auto vertices = [](Deco what, float yaw, float elev) {
    DecoFrame f;
    drawDeco(&f.dl, what, ImVec2(0, 0), ImVec2(400, 300), view::Theme::manifold(),
             IconStyle::Faceted, 0.0f, nullptr, 1.0f, 1.0f, yaw, elev);
    std::vector<float> out;
    out.reserve(static_cast<std::size_t>(f.dl.VtxBuffer.Size) * 2);
    for (int i = 0; i < f.dl.VtxBuffer.Size; ++i) {
      out.push_back(f.dl.VtxBuffer[i].pos.x);
      out.push_back(f.dl.VtxBuffer[i].pos.y);
    }
    return out;
  };
  for (const Deco what : {Deco::Manifold, Deco::Lattice, Deco::Tesseract}) {
    const std::vector<float> still = vertices(what, 0.0f, 0.0f);
    REQUIRE(!still.empty());
    const std::vector<float> turned = vertices(what, 0.7f, 0.0f);
    const std::vector<float> tipped = vertices(what, 0.0f, 0.5f);
    REQUIRE(turned.size() == still.size());
    REQUIRE(tipped.size() == still.size());
    CHECK(turned != still);
    CHECK(tipped != still);
  }
}

TEST_CASE("zooming a decoration scales it about its centre", "[render]") {
  DecoFrame one;
  drawDeco(&one.dl, Deco::Tesseract, ImVec2(0, 0), ImVec2(400, 300),
           view::Theme::manifold(), IconStyle::Faceted, 0.0f, nullptr, 1.0f, 1.0f);
  DecoFrame two;
  drawDeco(&two.dl, Deco::Tesseract, ImVec2(0, 0), ImVec2(400, 300),
           view::Theme::manifold(), IconStyle::Faceted, 0.0f, nullptr, 2.0f, 1.0f);

  const auto [w1, h1] = extent(one.dl);
  const auto [w2, h2] = extent(two.dl);
  REQUIRE(w1 > 0.0f);
  REQUIRE(h1 > 0.0f);
  CHECK(w2 > w1 * 1.8f);
  CHECK(h2 > h1 * 1.8f);
}

#endif
