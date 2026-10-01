// SPDX-License-Identifier: GPL-3.0-or-later
//
// The overtures, as geometry. Every claim the design rests on is here: a scene is a pure
// function of progress, every overture starts from the same flat 8x8, a cut keeps the
// cells it did not cut at their own size, and a Klein bottle's seams actually meet.
//
// None of it needs a GPU. The scene is plain arithmetic, and the draw path is assertions
// about a draw list - which is the reason the geometry lives apart from the drawing.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#ifdef CB_HAVE_IMGUI

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <imgui.h>

#include "io/variant_toml.hpp"
#include "render/overture_scene.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::render;
using Catch::Matchers::WithinAbs;

namespace {

/// Every overture that ships, so a new one cannot be added without these holding too.
const std::vector<app::Overture> kAll{
    app::Overture::Standard,    app::Overture::Cylinder,    app::Overture::Torus,
    app::Overture::Mobius,      app::Overture::Klein,       app::Overture::Mirrorbox,
    app::Overture::Cube5,       app::Overture::Hyper4,      app::Overture::Atomic,
    app::Overture::AtomicTorus, app::Overture::MustCapture, app::Overture::Multiverse,
    app::Overture::Torus3d,     app::Overture::T6};

float dist(const OvVec3& a, const OvVec3& b) {
  return std::hypot(std::hypot(a.x - b.x, a.y - b.y), a.z - b.z);
}

/// The bounding box of everything a scene draws, so a test can talk about extent
/// without caring which member a point arrived in.
struct Bounds {
  float lo[3]{1e9f, 1e9f, 1e9f};
  float hi[3]{-1e9f, -1e9f, -1e9f};
  void eat(const OvVec3& p) {
    const float v[3]{p.x, p.y, p.z};
    for (int i = 0; i < 3; ++i) {
      lo[i] = std::min(lo[i], v[i]);
      hi[i] = std::max(hi[i], v[i]);
    }
  }
  [[nodiscard]] float span(int axis) const { return hi[axis] - lo[axis]; }
  [[nodiscard]] float mid(int axis) const { return (hi[axis] + lo[axis]) * 0.5f; }
};

Bounds quadBounds(const OvertureScene& s) {
  Bounds b;
  for (const OvQuad& q : s.quads) {
    for (const OvVec3& p : q.p) b.eat(p);
  }
  return b;
}

/// A draw list with a context behind it, so the draw path can run headlessly.
struct Frame {
  ImGuiContext* ctx{nullptr};
  ImDrawList dl;
  Frame() : ctx(ImGui::CreateContext()), dl(ImGui::GetDrawListSharedData()) {
    dl._ResetForNewFrame();
  }
  ~Frame() {
    if (ctx != nullptr) ImGui::DestroyContext(ctx);
  }
  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;
};

}  // namespace

TEST_CASE("a scene is a pure function of progress", "[render]") {
  // The claim the whole design rests on. If this can fail, reverse playback is not free
  // and a screenshot is not reproducible.
  const view::Theme th = view::Theme::manifold();
  for (const app::Overture o : kAll) {
    for (const float t : {0.0f, 0.17f, 0.5f, 0.83f, 1.0f}) {
      const OvertureScene a = overtureScene(o, t, false, th);
      const OvertureScene b = overtureScene(o, t, false, th);
      REQUIRE(a.quads.size() == b.quads.size());
      REQUIRE(a.tokens.size() == b.tokens.size());
      REQUIRE(a.trails.size() == b.trails.size());
      REQUIRE(a.bursts.size() == b.bursts.size());
      for (std::size_t i = 0; i < a.quads.size(); ++i) {
        for (int j = 0; j < 4; ++j) {
          CHECK_THAT(dist(a.quads[i].p[j], b.quads[i].p[j]), WithinAbs(0.0f, 1e-9f));
        }
      }
      for (std::size_t i = 0; i < a.tokens.size(); ++i) {
        CHECK_THAT(dist(a.tokens[i].at, b.tokens[i].at), WithinAbs(0.0f, 1e-9f));
      }
      CHECK(a.caption == b.caption);
    }
  }
}

TEST_CASE("every overture opens on the same flat eight by eight", "[render]") {
  // The common ground. An overture the player can reach from any other has to hand over
  // at a board they recognise, or the queue's unwind buys nothing.
  const view::Theme th = view::Theme::manifold();
  for (const app::Overture o : kAll) {
    const OvertureScene s = overtureScene(o, 0.0f, false, th);
    const Bounds b = quadBounds(s);
    CHECK(s.quads.size() >= 64);
    // Eight units across, eight deep, and lying flat in y = 0.
    CHECK_THAT(b.span(0), WithinAbs(8.0f, 0.2f));
    CHECK_THAT(b.span(2), WithinAbs(8.0f, 0.2f));
    CHECK_THAT(b.span(1), WithinAbs(0.0f, 0.05f));
    // And centred, so it does not start from the edge of the pane.
    CHECK_THAT(b.mid(0), WithinAbs(0.0f, 0.1f));
    CHECK_THAT(b.mid(2), WithinAbs(0.0f, 0.1f));
  }
}

TEST_CASE("the board is seen from above", "[render]") {
  // White's home rank is the near one, and near is *down* the screen and drawn last.
  // Getting this backwards does not look like a bug, it looks like standing under the
  // board - the far edge comes out wider than the near one - so it is pinned here
  // rather than left to the eye.
  const view::Theme th = view::Theme::manifold();
  Frame f;
  const OvertureScene s = overtureScene(app::Overture::Standard, 1.0f, false, th);
  drawOverture(&f.dl, s, ImVec2(0, 0), ImVec2(600, 600), th, IconStyle::Faceted, 1.0f,
               1.0f);
  REQUIRE(f.dl.VtxBuffer.Size > 0);

  // Rank 1 sits at z = -3.5 and rank 8 at +3.5. Project both through the same path the
  // renderer uses and compare: the near rank must be lower on screen and wider.
  const auto rankRow = [&](float z) {
    Frame g;
    OvertureScene one;
    one.cam = s.cam;
    OvQuad q;
    q.p[0] = {-4.0f, 0.0f, z - 0.5f};
    q.p[1] = {4.0f, 0.0f, z - 0.5f};
    q.p[2] = {4.0f, 0.0f, z + 0.5f};
    q.p[3] = {-4.0f, 0.0f, z + 0.5f};
    one.quads.push_back(q);
    // A second, fixed quad so the auto-fit sees the same extent for both rows and the
    // comparison is about the projection rather than about the framing.
    OvQuad ref = q;
    ref.p[0] = {-4.0f, 0.0f, -4.0f};
    ref.p[1] = {4.0f, 0.0f, -4.0f};
    ref.p[2] = {4.0f, 0.0f, 4.0f};
    ref.p[3] = {-4.0f, 0.0f, 4.0f};
    one.quads.push_back(ref);
    drawOverture(&g.dl, one, ImVec2(0, 0), ImVec2(600, 600), th, IconStyle::Faceted, 1.0f,
                 1.0f);
    float loX = 1e9f, hiX = -1e9f, sumY = 0.0f;
    // The first quad's vertices come first in the buffer.
    const int n = std::min(4, g.dl.VtxBuffer.Size);
    for (int i = 0; i < n; ++i) {
      loX = std::min(loX, g.dl.VtxBuffer[i].pos.x);
      hiX = std::max(hiX, g.dl.VtxBuffer[i].pos.x);
      sumY += g.dl.VtxBuffer[i].pos.y;
    }
    return std::pair{sumY / static_cast<float>(n), hiX - loX};
  };
  const auto [nearY, nearW] = rankRow(-3.5f);
  const auto [farY, farW] = rankRow(3.5f);
  INFO("near y=" << nearY << " w=" << nearW << "  far y=" << farY << " w=" << farW);
  CHECK(nearY > farY);  // rank 1 lower on screen
  CHECK(nearW > farW);  // and wider, because it is nearer
}

TEST_CASE("pieces stand on top of the board", "[render]") {
  // The surface normal decides which side of a cell a piece is placed on, and getting
  // its sign backwards does not fail loudly - it silently renders every overture from
  // underneath. On the flat board the answer is not a matter of taste: up is +Y.
  const view::Theme th = view::Theme::manifold();
  const OvertureScene flat = overtureScene(app::Overture::Standard, 1.0f, false, th);
  REQUIRE(!flat.tokens.empty());
  for (const OvToken& k : flat.tokens) {
    CHECK(k.normal.y > 0.9f);
  }
  // And on a closed surface they stand *outward*, not into it. The cylinder is where
  // that is unambiguous: rolling the files gives a tube whose axis is the line
  // x = 0, y = -R with R = 8 / 2pi, so "outward" is simply "away from that line".
  const OvertureScene tubeS = overtureScene(app::Overture::Cylinder, 1.0f, false, th);
  REQUIRE(!tubeS.tokens.empty());
  constexpr float kR = 8.0f / 6.28318531f;
  for (const OvToken& k : tubeS.tokens) {
    const float ox = k.at.x;
    const float oy = k.at.y + kR;
    INFO("token at " << k.at.x << "," << k.at.y << " normal " << k.normal.x << ","
                     << k.normal.y);
    CHECK(k.normal.x * ox + k.normal.y * oy > 0.0f);
  }
}

TEST_CASE("every overture opens on the identical picture", "[render]") {
  // Stronger than "each starts from an 8 x 8", and it is what makes the cycle read as a
  // loop: the flat board every overture passes through has to be the *same* board, seen
  // from the *same* angle, with the *same* army on it. Otherwise leaving one overture
  // and arriving at the next is a cut, and the hand-over the queue works so hard to
  // arrange is thrown away in the frame it happens.
  const view::Theme th = view::Theme::manifold();
  const OvertureScene ref = overtureScene(app::Overture::Standard, 0.0f, false, th);

  // The camera, exactly.
  for (const app::Overture o : kAll) {
    const OvertureScene s = overtureScene(o, 0.0f, false, th);
    INFO("overture " << app::overtureName(o));
    CHECK_THAT(s.cam.yaw, WithinAbs(ref.cam.yaw, 1e-6f));
    CHECK_THAT(s.cam.elev, WithinAbs(ref.cam.elev, 1e-6f));
    CHECK_THAT(s.cam.persp, WithinAbs(ref.cam.persp, 1e-6f));
  }

  // The board. Quad *counts* legitimately differ - a curved geometry subdivides its
  // cells so the surface can bend - so what is compared is what subdivision preserves:
  // the area covered, and where it is.
  const auto area = [](const OvertureScene& s) {
    float total = 0.0f;
    for (const OvQuad& q : s.quads) {
      const OvVec3 a{q.p[1].x - q.p[0].x, q.p[1].y - q.p[0].y, q.p[1].z - q.p[0].z};
      const OvVec3 b{q.p[3].x - q.p[0].x, q.p[3].y - q.p[0].y, q.p[3].z - q.p[0].z};
      total += std::hypot(std::hypot(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z),
                          a.x * b.y - a.y * b.x);
    }
    return total;
  };
  const float refArea = area(ref);
  const Bounds refBox = quadBounds(ref);
  REQUIRE(refArea > 50.0f);
  for (const app::Overture o : kAll) {
    const OvertureScene s = overtureScene(o, 0.0f, false, th);
    const Bounds b = quadBounds(s);
    INFO("overture " << app::overtureName(o) << " area " << area(s) << " against "
                     << refArea);
    CHECK_THAT(area(s), WithinAbs(refArea, refArea * 0.02f));
    for (int ax = 0; ax < 3; ++ax) {
      CHECK_THAT(b.span(ax), WithinAbs(refBox.span(ax), 0.02f));
      CHECK_THAT(b.mid(ax), WithinAbs(refBox.mid(ax), 0.02f));
    }
  }

  // And the army: the same thirty-two pieces, on the same squares, the same way up.
  const auto roster = [](const OvertureScene& s) {
    std::vector<std::string> out;
    for (const OvToken& k : s.tokens) {
      if (k.fade <= 0.02f) continue;
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%c%c %.2f %.2f %.2f", k.white ? 'w' : 'b', k.glyph,
                    static_cast<double>(k.at.x), static_cast<double>(k.at.y),
                    static_cast<double>(k.at.z));
      out.emplace_back(buf);
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  const std::vector<std::string> refRoster = roster(ref);
  REQUIRE(refRoster.size() == 32);
  for (const app::Overture o : kAll) {
    INFO("overture " << app::overtureName(o));
    CHECK(roster(overtureScene(o, 0.0f, false, th)) == refRoster);
  }

  // Nothing is drawn on top of it either - no seam rim, no board outline, no wall.
  for (const app::Overture o : kAll) {
    const OvertureScene s = overtureScene(o, 0.0f, false, th);
    INFO("overture " << app::overtureName(o));
    for (const OvTrail& tr : s.trails) CHECK(tr.fade <= 0.001f);
    for (const OvBurst& b : s.bursts) CHECK(b.fade <= 0.001f);
  }

  // The one exception, and it is deliberate: the construction opens on an empty pane,
  // because building the board out of nothing is the whole of what it shows. It runs
  // once per session and is never handed over to.
  const OvertureScene built = overtureScene(app::Overture::Standard, 0.0f, true, th);
  CHECK(built.quads.empty());
}

TEST_CASE("the standard overture builds its board only when asked", "[render]") {
  const view::Theme th = view::Theme::manifold();
  // With the construction, the board arrives over the first third: early on, most cells
  // are not down yet and the ones that are have height still to lose.
  const OvertureScene building = overtureScene(app::Overture::Standard, 0.12f, true, th);
  const OvertureScene already = overtureScene(app::Overture::Standard, 0.12f, false, th);
  CHECK(building.quads.size() < already.quads.size());
  CHECK(quadBounds(building).span(1) > 0.5f);  // still falling
  CHECK(quadBounds(already).span(1) < 0.05f);  // simply there

  // And at the formed shape the two are the same picture, which is what lets the flag
  // drop at t = 1 with nothing visible happening.
  const OvertureScene endIntro = overtureScene(app::Overture::Standard, 1.0f, true, th);
  const OvertureScene endPlain = overtureScene(app::Overture::Standard, 1.0f, false, th);
  REQUIRE(endIntro.quads.size() == endPlain.quads.size());
  REQUIRE(endIntro.tokens.size() == endPlain.tokens.size());
  for (std::size_t i = 0; i < endIntro.tokens.size(); ++i) {
    CHECK_THAT(dist(endIntro.tokens[i].at, endPlain.tokens[i].at),
               WithinAbs(0.0f, 1e-5f));
    CHECK_THAT(endIntro.tokens[i].fade - endPlain.tokens[i].fade, WithinAbs(0.0f, 1e-5f));
  }

  // No other overture has a construction to honour, so the flag must not move them.
  for (const app::Overture o : kAll) {
    if (o == app::Overture::Standard) continue;
    const OvertureScene a = overtureScene(o, 0.3f, false, th);
    const OvertureScene b = overtureScene(o, 0.3f, true, th);
    CHECK(a.quads.size() == b.quads.size());
    CHECK(a.caption == b.caption);
  }
}

TEST_CASE("a cut drops rows and keeps the cells it did not cut", "[render]") {
  // The fix that matters here: scaling a board down to five units wide reads as a
  // smaller board laid on top of the big one. A cut keeps every survivor's own size, so
  // a cell is one unit before and after - and what is left is a corner block, not a
  // concentric one.
  const view::Theme th = view::Theme::manifold();
  for (const auto& [which, n] :
       {std::pair{app::Overture::Cube5, 5}, std::pair{app::Overture::Hyper4, 4},
        std::pair{app::Overture::Multiverse, 4}}) {
    // Measure one cell before the cut and the same cell after it.
    const OvertureScene before = overtureScene(which, 0.0f, false, th);
    REQUIRE(!before.quads.empty());
    const float cellBefore = dist(before.quads[0].p[0], before.quads[0].p[1]);

    // Partway in, some rows are gone and some remain: a cut mid-flight is the frame
    // where two board sizes would be visible at once if this were a rescale. Every cell
    // drawn must still be about a unit across - the small spread is the drawing gap
    // between cells, which differs between the flat board and the blocks on purpose and
    // is nothing to do with how big a cell is.
    REQUIRE(cellBefore > 0.9f);
    const OvertureScene mid = overtureScene(which, 0.08f, false, th);
    for (const OvQuad& q : mid.quads) {
      const float e = dist(q.p[0], q.p[1]);
      INFO("cell edge " << e << " against " << cellBefore);
      CHECK(e > 0.85f);
      CHECK(e <= 1.0f);
    }

    // Once the cut is done, the block that survived is n cells on a side.
    const float done = which == app::Overture::Cube5 ? 0.34f : 0.16f;
    const Bounds b = quadBounds(overtureScene(which, done, false, th));
    CHECK_THAT(b.span(0), WithinAbs(static_cast<float>(n), 0.25f));
    CHECK_THAT(b.span(2), WithinAbs(static_cast<float>(n), 0.25f));
    // And it is the a1 corner, still off-centre: the recentring is a separate motion,
    // which is what makes the cut read as a cut.
    CHECK(b.mid(0) < -0.5f);
    CHECK(b.mid(2) < -0.5f);
  }
}

TEST_CASE("a cut block is centred once it has slid", "[render]") {
  const view::Theme th = view::Theme::manifold();
  // cube5 cuts to 0.34 and recentres by 0.44; hyper4 cuts to 0.16 and recentres by 0.24.
  const Bounds cube = quadBounds(overtureScene(app::Overture::Cube5, 0.44f, false, th));
  CHECK_THAT(cube.mid(0), WithinAbs(0.0f, 0.2f));
  CHECK_THAT(cube.mid(2), WithinAbs(0.0f, 0.2f));
  const Bounds hyper = quadBounds(overtureScene(app::Overture::Hyper4, 0.24f, false, th));
  CHECK_THAT(hyper.mid(0), WithinAbs(0.2f, 0.4f));
}

TEST_CASE("the multiverse opens on one board in the middle", "[render]") {
  // It used to start from the edge of the pane. The lattice grows around the first
  // board, so at every t the boards drawn stay centred on what the camera is looking at.
  const view::Theme th = view::Theme::manifold();
  // From the moment the cut has slid, and for the whole of the rest of the cycle. Before
  // that the board is deliberately off-centre: the cut takes the h-file and the 8th rank,
  // so what is left leans to a1 until it moves.
  for (const float t : {0.22f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f}) {
    const Bounds b = quadBounds(overtureScene(app::Overture::Multiverse, t, false, th));
    INFO("at t=" << t);
    CHECK_THAT(b.mid(0), WithinAbs(0.0f, 1.0f));
    CHECK_THAT(b.mid(2), WithinAbs(0.0f, 1.0f));
  }
  // And the very first frame is the ordinary centred 8 x 8.
  const Bounds first =
      quadBounds(overtureScene(app::Overture::Multiverse, 0.0f, false, th));
  CHECK_THAT(first.mid(0), WithinAbs(0.0f, 0.1f));
  // And it does grow: seven boards by the end is wider than one at the start.
  const Bounds one =
      quadBounds(overtureScene(app::Overture::Multiverse, 0.22f, false, th));
  const Bounds many =
      quadBounds(overtureScene(app::Overture::Multiverse, 1.0f, false, th));
  CHECK(many.span(0) > one.span(0) * 2.5f);
}

TEST_CASE("each gluing overture closes the seams its variant declares", "[render]") {
  // The bug this exists for. `klein` joins its rank edges with flip = ["file"], so the
  // formed surface must send (u, 0) to (1 - u, 1) - the file reversed. A *circular*
  // cross-section given a half-turn comes back shifted by four files instead, which is a
  // different surface and leaves the two rims visibly not meeting. A figure-eight
  // rotated by pi maps (sin a, sin 2a) to (sin -a, sin -2a), which is the reflection.
  //
  // Each case below is the variant's own `[[geometry.identify]]` block, restated as
  // arithmetic: what must meet, and - just as importantly - what must not.
  const auto at = [](app::Overture o, float u, float v) {
    return overtureSurfaceAt(o, u, v);
  };
  constexpr float kMeet = 0.02f;  // a hundredth of a cell
  constexpr float kApart = 0.8f;  // distinctly not the same point

  SECTION("cylinder: the files join, the ranks keep their walls") {
    for (int i = 0; i <= 8; ++i) {
      const float v = static_cast<float>(i) / 8.0f;
      CHECK_THAT(dist(at(app::Overture::Cylinder, 0.0f, v),
                      at(app::Overture::Cylinder, 1.0f, v)),
                 WithinAbs(0.0f, kMeet));
    }
    // The rank axis is still a wall - that is the whole difference from the torus.
    for (int i = 1; i < 8; ++i) {
      const float u = static_cast<float>(i) / 8.0f;
      CHECK(dist(at(app::Overture::Cylinder, u, 0.0f),
                 at(app::Overture::Cylinder, u, 1.0f)) > kApart);
    }
  }

  SECTION("torus: both pairs join, straight") {
    for (int i = 0; i <= 8; ++i) {
      const float q = static_cast<float>(i) / 8.0f;
      CHECK_THAT(
          dist(at(app::Overture::Torus, 0.0f, q), at(app::Overture::Torus, 1.0f, q)),
          WithinAbs(0.0f, kMeet));
      CHECK_THAT(
          dist(at(app::Overture::Torus, q, 0.0f), at(app::Overture::Torus, q, 1.0f)),
          WithinAbs(0.0f, kMeet));
    }
  }

  SECTION("mobius: the files join with the rank flipped") {
    for (int i = 0; i <= 8; ++i) {
      const float v = static_cast<float>(i) / 8.0f;
      CHECK_THAT(dist(at(app::Overture::Mobius, 0.0f, v),
                      at(app::Overture::Mobius, 1.0f, 1.0f - v)),
                 WithinAbs(0.0f, kMeet));
    }
    // And *not* straight: a Moebius band is not a cylinder, so away from the middle the
    // unflipped join must fail.
    CHECK(dist(at(app::Overture::Mobius, 0.0f, 0.1f),
               at(app::Overture::Mobius, 1.0f, 0.1f)) > kApart);
  }

  SECTION("klein: the files join straight, the ranks join with the file flipped") {
    for (int i = 0; i <= 8; ++i) {
      const float q = static_cast<float>(i) / 8.0f;
      CHECK_THAT(
          dist(at(app::Overture::Klein, 0.0f, q), at(app::Overture::Klein, 1.0f, q)),
          WithinAbs(0.0f, kMeet));
      // The one that the circular cross-section could not do.
      CHECK_THAT(dist(at(app::Overture::Klein, q, 0.0f),
                      at(app::Overture::Klein, 1.0f - q, 1.0f)),
                 WithinAbs(0.0f, kMeet));
    }
    // It is not a plain torus: joining the ranks *without* the file flip must fail,
    // or the overture is drawing the wrong surface and claiming it is a Klein bottle.
    int wrong = 0;
    for (int i = 1; i < 8; ++i) {
      const float q = static_cast<float>(i) / 8.0f;
      if (dist(at(app::Overture::Klein, q, 0.0f), at(app::Overture::Klein, q, 1.0f)) >
          kApart) {
        ++wrong;
      }
    }
    CHECK(wrong >= 5);
  }

  SECTION("atomic_torus glues exactly as the torus does") {
    for (int i = 0; i <= 8; ++i) {
      const float q = static_cast<float>(i) / 8.0f;
      CHECK_THAT(dist(at(app::Overture::AtomicTorus, q, 0.0f),
                      at(app::Overture::AtomicTorus, q, 1.0f)),
                 WithinAbs(0.0f, kMeet));
    }
  }
}

TEST_CASE("the derived surface equals the hand-authored one for its topology",
          "[render]") {
  // M13.2: deriving a surface is only safe if it agrees with the surfaces already
  // authored. The shipped scenes are the oracle, so a generated cylinder/torus/band/klein
  // has to land on exactly the same points.
  struct Row {
    app::SurfaceKind kind;
    app::Overture authored;
  };
  const Row rows[]{
      {app::SurfaceKind::Tube, app::Overture::Cylinder},
      {app::SurfaceKind::Torus, app::Overture::Torus},
      {app::SurfaceKind::Band, app::Overture::Mobius},
      {app::SurfaceKind::Klein, app::Overture::Klein},
  };
  for (const Row& r : rows) {
    for (int i = 0; i <= 8; ++i) {
      for (int j = 0; j <= 8; ++j) {
        const float u = static_cast<float>(i) / 8.0f;
        const float v = static_cast<float>(j) / 8.0f;
        INFO("kind " << static_cast<int>(r.kind) << " at " << u << "," << v);
        CHECK_THAT(
            dist(derivedSurfaceAt(r.kind, u, v), overtureSurfaceAt(r.authored, u, v)),
            WithinAbs(0.0f, 1e-5f));
      }
    }
  }
}

TEST_CASE("a data-only variant gets a derived overture", "[render]") {
  // A torus built only as TOML text: its name is in no C++ table, so it animates with the
  // derived scene and not a line of scene code written for it.
  const std::string toml =
      "name = \"workshop_torus\"\n"
      "description = \"a torus nobody wrote an overture for\"\n"
      "[[axis]]\nname = \"file\"\nextent = 8\n"
      "[[axis]]\nname = \"rank\"\nextent = 8\n"
      "[[geometry.identify]]\naxis = \"file\"\nkind = \"periodic\"\n"
      "[[geometry.identify]]\naxis = \"rank\"\nkind = \"periodic\"\n"
      "[[piece]]\nname = \"rook\"\nsymbol = \"R\"\n"
      "[[piece.move]]\nvector = [1]\nmax = \"inf\"\nmode = \"slide\"\n"
      "[[piece]]\nname = \"king\"\nsymbol = \"K\"\nroyal = true\n"
      "[[piece.move]]\nvector = [1]\nmax = 1\nmode = \"leap\"\n"
      "[[piece.move]]\nvector = [1, 1]\nmax = 1\nmode = \"leap\"\n"
      "[start]\nboard = \"4k3/8/8/8/8/8/8/R3K3\"\n";
  const auto v = loadVariantToml(toml, "<derived>");
  REQUIRE(v.has_value());
  CHECK_FALSE(app::hasBespokeOverture(v->name));

  const view::Theme th = view::Theme::manifold();
  for (const float t : {0.0f, 0.5f, 1.0f}) {
    const OvertureScene a = derivedOvertureScene(*v, t, th);
    const OvertureScene b = derivedOvertureScene(*v, t, th);
    // Pure, and never blank.
    CHECK(a.quads.size() >= 64);
    REQUIRE(a.quads.size() == b.quads.size());
    for (std::size_t i = 0; i < a.quads.size(); ++i) {
      for (int j = 0; j < 4; ++j) {
        CHECK_THAT(dist(a.quads[i].p[j], b.quads[i].p[j]), WithinAbs(0.0f, 1e-9f));
        CHECK(std::isfinite(a.quads[i].p[j].x));
      }
    }
  }

  // The demo move (M13.3): the variant's own pieces, and a real traced route drawn as a
  // trail plus the arriving token. Two kings and a rook on the board, and the rook's
  // route adds at least one more token than the start position alone.
  const OvertureScene formed = derivedOvertureScene(*v, 0.7f, th);
  CHECK(formed.tokens.size() >= 4);
  CHECK(formed.trails.size() > 4);  // four seam rims, plus the move
}

TEST_CASE("an overture at alpha zero leaves no geometry behind", "[render]") {
  // The same contract every decoration keeps, so a screen on its way out takes its
  // object with it.
  const view::Theme th = view::Theme::manifold();
  Frame gone;
  drawOverture(&gone.dl, overtureScene(app::Overture::Torus, 0.6f, false, th),
               ImVec2(0, 0), ImVec2(400, 300), th, IconStyle::Faceted, 1.0f, 0.0f);
  CHECK(gone.dl.VtxBuffer.Size == 0);

  // And a visible one does draw, so the check above is not vacuous.
  Frame there;
  drawOverture(&there.dl, overtureScene(app::Overture::Torus, 0.6f, false, th),
               ImVec2(0, 0), ImVec2(400, 300), th, IconStyle::Faceted, 1.0f, 1.0f);
  CHECK(there.dl.VtxBuffer.Size > 0);
}

TEST_CASE("no overture ever emits a point that is not a number", "[render]") {
  // A NaN in a warp reaches the screen as nothing at all - and worse, it makes the
  // depth sort's comparator non-transitive, which is undefined behaviour and took down
  // the whole suite rather than failing one assertion. Checked on the scene, before
  // anything can sort it.
  const view::Theme th = view::Theme::manifold();
  for (const app::Overture o : kAll) {
    for (int i = 0; i <= 40; ++i) {
      const float t = static_cast<float>(i) / 40.0f;
      const OvertureScene s = overtureScene(o, t, i % 2 == 0, th);
      INFO("overture " << app::overtureName(o) << " at t=" << t);
      const auto finite = [](const OvVec3& p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
      };
      for (const OvQuad& q : s.quads) {
        for (const OvVec3& p : q.p) REQUIRE(finite(p));
        REQUIRE(std::isfinite(q.fade));
      }
      for (const OvToken& k : s.tokens) {
        REQUIRE(finite(k.at));
        REQUIRE(finite(k.normal));
        REQUIRE(std::isfinite(k.fade));
      }
      for (const OvTrail& tr : s.trails) {
        for (const OvVec3& p : tr.pts) REQUIRE(finite(p));
      }
      for (const OvBurst& b : s.bursts) {
        REQUIRE(finite(b.at));
        REQUIRE(std::isfinite(b.radius));
        REQUIRE(std::isfinite(b.fade));
      }
      REQUIRE(std::isfinite(s.cam.yaw));
      REQUIRE(std::isfinite(s.cam.elev));
      REQUIRE(std::isfinite(s.cam.reach));
      REQUIRE(s.cam.reach > 0.0f);
    }
  }
}

TEST_CASE("every overture draws something at every point of its cycle", "[render]") {
  // A blank pane is the failure this catches: a phase boundary that leaves nothing on
  // screen, or a camera whose reach collapsed and divided the object to nothing.
  const view::Theme th = view::Theme::manifold();
  for (const app::Overture o : kAll) {
    for (int i = 0; i <= 20; ++i) {
      const float t = static_cast<float>(i) / 20.0f;
      Frame f;
      drawOverture(&f.dl, overtureScene(o, t, false, th), ImVec2(0, 0), ImVec2(600, 400),
                   th, IconStyle::Faceted, 1.0f, 1.0f);
      INFO("overture " << app::overtureName(o) << " at t=" << t);
      CHECK(f.dl.VtxBuffer.Size > 0);
      // Every vertex has to be a number. A NaN here is how a divide-by-zero in a warp
      // reaches the screen, and ImGui will happily draw it as nothing at all.
      for (int v = 0; v < f.dl.VtxBuffer.Size; ++v) {
        REQUIRE(std::isfinite(f.dl.VtxBuffer[v].pos.x));
        REQUIRE(std::isfinite(f.dl.VtxBuffer[v].pos.y));
      }
    }
  }
}

TEST_CASE("zooming an overture scales it about the centre", "[render]") {
  const view::Theme th = view::Theme::manifold();
  const auto extent = [&](float zoom) {
    Frame f;
    drawOverture(&f.dl, overtureScene(app::Overture::Cube5, 0.9f, false, th),
                 ImVec2(0, 0), ImVec2(400, 300), th, IconStyle::Faceted, zoom, 1.0f);
    REQUIRE(f.dl.VtxBuffer.Size > 0);
    float lo = f.dl.VtxBuffer[0].pos.x;
    float hi = lo;
    for (int i = 1; i < f.dl.VtxBuffer.Size; ++i) {
      lo = std::min(lo, f.dl.VtxBuffer[i].pos.x);
      hi = std::max(hi, f.dl.VtxBuffer[i].pos.x);
    }
    return hi - lo;
  };
  const float one = extent(1.0f);
  REQUIRE(one > 0.0f);
  CHECK(extent(2.0f) > one * 1.8f);
}

TEST_CASE("torus3d lights a king's twenty-six neighbours", "[render]") {
  // The one number the variant exists for: on a fully glued 4x4x4 no direction is ever
  // clipped, so the king's neighbourhood is the whole 26-cell block about it. The
  // overture is the only place that count is stated, so it is pinned here.
  const view::Theme th = view::Theme::manifold();
  const OvertureScene s = overtureScene(app::Overture::Torus3d, 1.0f, false, th);
  int lit = 0;
  for (const OvBurst& b : s.bursts) {
    if (b.fade > 0.02f) ++lit;
  }
  CHECK(lit == 26);
}

TEST_CASE("t6 lights sixty cells for a hundred and twenty directions", "[render]") {
  // The knight's atom at six axes is P(6,2)*4 = 120 directions, but on an axis of extent
  // 4 the two signs of the magnitude-2 leg coincide, so 120 vectors land on 60 cells. The
  // overture draws the 60; the caption speaks the 120; and the two agreeing is the point.
  const view::Theme th = view::Theme::manifold();
  const OvertureScene s = overtureScene(app::Overture::T6, 1.0f, false, th);
  int lit = 0;
  for (const OvBurst& b : s.bursts) {
    if (b.fade > 0.02f) ++lit;
  }
  CHECK(lit == 60);
  CHECK(s.caption.find("hundred and twenty") != std::string::npos);
}

TEST_CASE("a detonation spends no geometry colour", "[render]") {
  // The palette rule, made mechanical. A detonation is a *game* event, so it takes
  // `blood` and `ember`; spending a seam hue on it would break the one thing a player
  // learns by playing - that the cold ramp means the board is not where it looks.
  const auto near = [](const view::Rgba& a, const view::Rgba& b) {
    return std::abs(a.r - b.r) < 0.02f && std::abs(a.g - b.g) < 0.02f &&
           std::abs(a.b - b.b) < 0.02f;
  };
  const view::Theme kThemes[]{view::Theme::manifold(), view::Theme::console()};
  for (const view::Theme& th : kThemes) {
    for (const app::Overture o : {app::Overture::Atomic, app::Overture::AtomicTorus}) {
      const OvertureScene s = overtureScene(o, 0.62f, false, th);
      INFO("overture " << app::overtureName(o));
      REQUIRE(!s.bursts.empty());
      for (const OvBurst& b : s.bursts) {
        CHECK((near(b.colour, th.blood) || near(b.colour, th.ember)));
      }
    }
  }
}

TEST_CASE("only a glued two-dimensional variant has a play surface", "[render]") {
  // The gate the interface reads before offering the toggle. `standard`, a mirror box and
  // every board above two dimensions have no second shape to become, and a button that
  // did nothing there would be a lie (M17.5).
  CHECK_FALSE(hasPlaySurface(test::loadVariant("standard")));
  CHECK_FALSE(hasPlaySurface(test::loadVariant("mirrorbox")));
  CHECK_FALSE(hasPlaySurface(test::loadVariant("cube5")));
  CHECK(hasPlaySurface(test::loadVariant("cylinder")));
  CHECK(hasPlaySurface(test::loadVariant("torus")));
  CHECK(hasPlaySurface(test::loadVariant("mobius")));
  CHECK(hasPlaySurface(test::loadVariant("klein")));
}

#endif
