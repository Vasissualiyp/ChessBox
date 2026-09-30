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
