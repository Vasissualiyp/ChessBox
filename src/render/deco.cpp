// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/deco.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "app/shell.hpp"
#include "render/ui_widgets.hpp"

namespace cb::render {
namespace {

using widgets::u32;

constexpr float kPi = 3.14159265358979f;

/// A deterministic generator. The shell has to draw the same frame twice - that is how
/// the headless capture tests see it at all - so nothing here may reach for a clock or
/// a global random source.
float nextFloat(std::uint32_t& s) {
  s = s * 1664525u + 1013904223u;
  return static_cast<float>((s >> 8) & 0xFFFFFF) / static_cast<float>(0x1000000);
}

struct Vec3 {
  float x{0}, y{0}, z{0};
};

Vec3 rotateY(Vec3 p, float a) {
  return {p.x * std::cos(a) + p.z * std::sin(a), p.y,
          -p.x * std::sin(a) + p.z * std::cos(a)};
}
Vec3 rotateX(Vec3 p, float a) {
  return {p.x, p.y * std::cos(a) - p.z * std::sin(a), p.y * std::sin(a) + p.z * std::cos(a)};
}

/// Where a scene point lands, plus the depth it landed from - the depth is the only
/// cue a flat drawing has, so it is carried rather than discarded.
struct Projected {
  ImVec2 at;
  float depth{0};
};

struct Camera {
  ImVec2 centre;
  float scale{1};
  float yaw{0}, pitch{0};
  float perspective{0.16f};

  [[nodiscard]] Projected operator()(Vec3 p) const {
    const Vec3 q = rotateX(rotateY(p, yaw), pitch);
    const float k = 1.0f / (1.0f + q.z * perspective);
    return {ImVec2(centre.x + q.x * scale * k, centre.y - q.y * scale * k), q.z};
  }
};

view::Rgba withAlpha(view::Rgba c, float a) {
  c.a = a;
  return c;
}

/// Draw one piece icon, filled and outlined, centred on a point.
void icon(ImDrawList* dl, IconStyle style, Archetype shape, ImVec2 centre, float size,
          ImU32 fill, ImU32 line) {
  const PieceIcon art = pieceIcon(style, shape);
  const float k = size / 100.0f;
  for (const IconPoly& poly : art.fills) {
    if (poly.size() < 3) continue;
    dl->PathClear();
    for (const IconPoint& p : poly) {
      dl->PathLineTo(ImVec2(centre.x + (p.x - 50.0f) * k, centre.y + (p.y - 50.0f) * k));
    }
    // Stroke before fill would be lost under it; the outline is what keeps a pale piece
    // visible on a pale tile.
    const ImVector<ImVec2> path = dl->_Path;
    dl->PathFillConcave(fill);
    dl->_Path = path;
    dl->PathStroke(line, ImDrawFlags_Closed, std::max(1.0f, size * 0.022f));
  }
}

const Archetype kPieceShapes[]{Archetype::Tower, Archetype::Wedge,  Archetype::Crown,
                               Archetype::Spire, Archetype::Dome,   Archetype::Monolith,
                               Archetype::Horn};

// ---------------------------------------------------------------------------
// The Calabi-Yau quintic cross-section.
//
// z1 = e^(2*pi*i*k1/n) (cos a)^(2/n),  z2 = e^(2*pi*i*k2/n) (sin a)^(2/n),  a = x + iy,
// drawn as (Re z1, Re z2, Im z1 cos alpha + Im z2 sin alpha). That is the standard
// picture, not an impression of one - which matters, because the whole point of putting
// it beside the menu is that it is a real object.
// ---------------------------------------------------------------------------
constexpr int kQuinticN = 5;
constexpr float kQuinticAlpha = 0.5f;

void complexPow(float re, float im, float p, float& outRe, float& outIm) {
  const float r = std::hypot(re, im);
  const float th = std::atan2(im, re);
  const float rp = std::pow(r, p);
  outRe = rp * std::cos(th * p);
  outIm = rp * std::sin(th * p);
}

Vec3 quintic(int k1, int k2, float x, float y) {
  const float cRe = std::cos(x) * std::cosh(y);
  const float cIm = -std::sin(x) * std::sinh(y);
  const float sRe = std::sin(x) * std::cosh(y);
  const float sIm = std::cos(x) * std::sinh(y);
  float aRe = 0, aIm = 0, bRe = 0, bIm = 0;
  complexPow(cRe, cIm, 2.0f / kQuinticN, aRe, aIm);
  complexPow(sRe, sIm, 2.0f / kQuinticN, bRe, bIm);
  const float p1 = 2.0f * kPi * static_cast<float>(k1) / kQuinticN;
  const float p2 = 2.0f * kPi * static_cast<float>(k2) / kQuinticN;
  const float z1r = aRe * std::cos(p1) - aIm * std::sin(p1);
  const float z1i = aRe * std::sin(p1) + aIm * std::cos(p1);
  const float z2r = bRe * std::cos(p2) - bIm * std::sin(p2);
  const float z2i = bRe * std::sin(p2) + bIm * std::cos(p2);
  return {z1r, z2r,
          z1i * std::cos(kQuinticAlpha) + z2i * std::sin(kQuinticAlpha)};
}

void drawManifold(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                  IconStyle iconStyle, float t) {
  Camera cam;
  cam.centre = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  cam.scale = std::min(max.x - min.x, max.y - min.y) * 0.40f;
  cam.yaw = t * 0.09f;
  cam.pitch = 0.45f + std::sin(t * 0.05f) * 0.22f;

  constexpr int kSteps = 6;
  struct Tile {
    ImVec2 p[4];
    float depth{0};
    bool dark{false};
  };
  std::vector<Tile> tiles;
  tiles.reserve(kQuinticN * kQuinticN * kSteps * kSteps);

  for (int k1 = 0; k1 < kQuinticN; ++k1) {
    for (int k2 = 0; k2 < kQuinticN; ++k2) {
      Projected grid[kSteps + 1][kSteps + 1];
      for (int i = 0; i <= kSteps; ++i) {
        for (int j = 0; j <= kSteps; ++j) {
          const float fx = static_cast<float>(i) / kSteps * (kPi * 0.5f);
          const float fy = -1.0f + 2.0f * static_cast<float>(j) / kSteps;
          grid[i][j] = cam(quintic(k1, k2, fx, fy));
        }
      }
      for (int i = 0; i < kSteps; ++i) {
        for (int j = 0; j < kSteps; ++j) {
          Tile tile;
          tile.p[0] = grid[i][j].at;
          tile.p[1] = grid[i + 1][j].at;
          tile.p[2] = grid[i + 1][j + 1].at;
          tile.p[3] = grid[i][j + 1].at;
          tile.depth = (grid[i][j].depth + grid[i + 1][j].depth +
                        grid[i + 1][j + 1].depth + grid[i][j + 1].depth) *
                       0.25f;
          tile.dark = (i + j + k1 + k2) % 2 == 0;
          tiles.push_back(tile);
        }
      }
    }
  }

  // Far to near: there is no depth buffer in a draw list, so the order is the depth.
  std::sort(tiles.begin(), tiles.end(),
            [](const Tile& a, const Tile& b) { return a.depth < b.depth; });

  const view::Rgba dark = theme.light ? theme.bone : theme.ink;
  const view::Rgba pale = theme.light ? theme.panel : theme.bone;
  for (const Tile& tile : tiles) {
    const float near = std::clamp((tile.depth + 1.1f) / 2.2f, 0.0f, 1.0f);
    const float alpha = 0.13f + 0.26f * near;
    dl->AddQuadFilled(tile.p[0], tile.p[1], tile.p[2], tile.p[3],
                      u32(withAlpha(tile.dark ? dark : pale, alpha)));
    dl->AddQuad(tile.p[0], tile.p[1], tile.p[2], tile.p[3],
                u32(withAlpha(dark, 0.10f + 0.12f * near)), 1.0f);
  }

  // Both colours of piece, walking the surface's own parameter lines. Two-dimensional
  // pieces on a six-dimensional surface is the joke, and it is also the thesis.
  std::uint32_t seed = 0x5EED1234u;
  const float span = std::min(max.x - min.x, max.y - min.y);
  for (int i = 0; i < 14; ++i) {
    const int k1 = i % kQuinticN;
    const int k2 = (i * 2 + 1) % kQuinticN;
    const float du = (nextFloat(seed) - 0.5f) * 0.12f;
    const float dv = (nextFloat(seed) - 0.5f) * 0.12f;
    float u = nextFloat(seed) * 1.5f + t * du;
    float v = nextFloat(seed) * 2.0f - 1.0f + t * dv;
    u = std::fmod(std::fmod(u, kPi * 0.5f) + kPi * 0.5f, kPi * 0.5f);
    v = std::sin(v) * 0.85f;
    const Projected p = cam(quintic(k1, k2, u, v));
    const float size = span * (0.030f + 0.012f / (1.0f + std::abs(p.depth))) * 3.0f;
    const bool white = i % 2 == 0;
    icon(dl, iconStyle, kPieceShapes[static_cast<std::size_t>(i) % 7],
         ImVec2(p.at.x, p.at.y - size * 0.2f), size,
         u32(white ? pale : dark, 0.94f), u32(white ? dark : pale, 0.7f));
  }
}

void drawLattice(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                 IconStyle iconStyle, float t, const VariantSpec* variant) {
  // How many boards, and how deep: read off the variant rather than hardcoded, so the
  // decoration is genuinely about the thing you are choosing.
  int across = 1, down = 1, layers = 1;
  int n = 8;
  if (variant != nullptr) {
    n = std::clamp<int>(variant->dims.extent(0), 3, 10);
    int extra = 0;
    for (std::uint8_t a = 2; a < variant->dims.dims(); ++a) {
      const int e = std::clamp<int>(variant->dims.extent(a), 1, 3);
      if (extra == 0) {
        across = e;
      } else if (extra == 1) {
        down = e;
      } else {
        layers = std::max(layers, e);
      }
      ++extra;
    }
    if (variant->dims.dims() == 3) {
      layers = std::clamp<int>(variant->dims.extent(2), 1, 5);
      across = 1;
    }
  }

  Camera cam;
  cam.centre = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.52f);
  const float span = std::min(max.x - min.x, max.y - min.y);
  cam.scale = span * (across > 1 || down > 1 ? 0.050f : 0.082f);
  cam.yaw = t * 0.16f;
  cam.pitch = 0.95f + std::sin(t * 0.09f) * 0.10f;
  cam.perspective = 0.06f;

  const view::Rgba dark = theme.light ? theme.bone : theme.ink;
  const view::Rgba pale = theme.light ? theme.panel : theme.bone;
  const float half = static_cast<float>(n) * 0.5f;

  for (int bx = 0; bx < across; ++bx) {
    for (int by = 0; by < down; ++by) {
      for (int lz = 0; lz < layers; ++lz) {
        const float ox =
            (static_cast<float>(bx) - static_cast<float>(across - 1) * 0.5f) * (static_cast<float>(n) + 3);
        const float oy =
            (static_cast<float>(by) - static_cast<float>(down - 1) * 0.5f) * (static_cast<float>(n) + 3);
        const float oz = (static_cast<float>(lz) - static_cast<float>(layers - 1) * 0.5f) * 2.4f;
        for (int i = 0; i < n; ++i) {
          for (int j = 0; j < n; ++j) {
            const float x = ox - half + static_cast<float>(i);
            const float y = oy - half + static_cast<float>(j);
            const ImVec2 a = cam({x, y, oz}).at;
            const ImVec2 b = cam({x + 1, y, oz}).at;
            const ImVec2 c = cam({x + 1, y + 1, oz}).at;
            const ImVec2 d = cam({x, y + 1, oz}).at;
            const bool light = (i + j) % 2 != 0;
            dl->AddQuadFilled(a, b, c, d,
                              u32(withAlpha(light ? pale : dark, light ? 0.20f : 0.34f)));
          }
        }
        // Pieces on cell centres, sized to the cell. Scaling them off anything else is
        // how a nine-board lattice turns into a heap of overlapping shapes.
        for (int g = 0; g < 4; ++g) {
          const float x = ox - half + 0.5f + static_cast<float>(g) * 2.0f;
          const float y = oy - half + 0.5f + static_cast<float>((g + bx + by) % 3);
          const ImVec2 at = cam({x, y, oz}).at;
          const bool white = g % 2 == 0;
          icon(dl, iconStyle, kPieceShapes[static_cast<std::size_t>(g)],
               ImVec2(at.x, at.y - cam.scale * 0.2f), cam.scale * 0.92f,
               u32(white ? pale : dark, 0.95f), u32(white ? dark : pale, 0.6f));
        }
      }
    }
  }
}

void drawTesseract(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                   float t) {
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const float scale = std::min(max.x - min.x, max.y - min.y) * 0.26f;
  const float a = t * 0.14f, b = t * 0.09f;

  ImVec2 pts[16];
  for (int i = 0; i < 16; ++i) {
    const float x = static_cast<float>((i & 1) * 2 - 1);
    const float y = static_cast<float>(((i >> 1) & 1) * 2 - 1);
    const float z = static_cast<float>(((i >> 2) & 1) * 2 - 1);
    const float w = static_cast<float>(((i >> 3) & 1) * 2 - 1);
    // Turned in the xw and yz planes: the two rotations that make a 4-cube legible
    // rather than merely busy.
    const float nx = x * std::cos(a) - w * std::sin(a);
    const float nw = x * std::sin(a) + w * std::cos(a);
    const float ny = y * std::cos(b) - z * std::sin(b);
    const float nz = y * std::sin(b) + z * std::cos(b);
    const float k = 1.0f / (2.4f - nw * 0.85f);
    const Vec3 p = rotateX({nx * k * 2.2f, ny * k * 2.2f, nz * k * 2.2f}, 0.35f);
    const float pp = 1.0f / (1.0f + p.z * 0.2f);
    pts[i] = ImVec2(centre.x + p.x * scale * pp, centre.y - p.y * scale * pp);
  }

  const view::Rgba line = theme.light ? theme.bone : theme.boneDim;
  for (int i = 0; i < 16; ++i) {
    for (int d = 0; d < 4; ++d) {
      const int j = i ^ (1 << d);
      if (j < i) continue;
      // The fourth-axis edges are drawn fainter: they are the ones that are not really
      // there, and saying so is the whole reason a 4-cube is worth drawing.
      dl->AddLine(pts[i], pts[j], u32(withAlpha(line, d == 3 ? 0.20f : 0.42f)), 1.4f);
    }
  }
  for (const ImVec2& p : pts) {
    dl->AddRectFilled(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + 2, p.y + 2),
                      u32(withAlpha(line, 0.85f)));
  }
}

void drawAtom(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
              IconStyle iconStyle, float t) {
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const float cell = std::min(max.x - min.x, max.y - min.y) * 0.055f;
  const view::Rgba line = theme.light ? theme.bone : theme.boneDim;

  for (int i = -5; i <= 5; ++i) {
    const float o = static_cast<float>(i) * cell;
    dl->AddLine(ImVec2(centre.x - 5 * cell, centre.y + o),
                ImVec2(centre.x + 5 * cell, centre.y + o), u32(withAlpha(line, 0.16f)));
    dl->AddLine(ImVec2(centre.x + o, centre.y - 5 * cell),
                ImVec2(centre.x + o, centre.y + 5 * cell), u32(withAlpha(line, 0.16f)));
  }

  // [1,2] on two axes: every signed permutation, arriving one after another, which is
  // exactly what the expansion does when a variant is loaded.
  int index = 0;
  for (const int pair : {0, 1}) {
    const int p = pair == 0 ? 1 : 2;
    const int q = pair == 0 ? 2 : 1;
    for (const int sx : {1, -1}) {
      for (const int sy : {1, -1}) {
        // Every direction is always there - that is the point of an atom - and the
        // sweep only says which one is being counted right now. A still frame of this
        // has to show the whole expansion, not two arrows and a lot of empty grid.
        const ImVec2 full(centre.x + static_cast<float>(p * sx) * cell,
                          centre.y - static_cast<float>(q * sy) * cell);
        dl->AddLine(centre, full, u32(withAlpha(line, 0.30f)), 1.4f);
        dl->AddCircleFilled(full, cell * 0.11f, u32(withAlpha(line, 0.45f)), 10);

        const float phase =
            std::fmod(t * 0.7f - static_cast<float>(index) * 0.34f, 5.6f);
        ++index;
        const float k = std::clamp(phase, 0.0f, 1.0f);
        if (k <= 0.0f) continue;
        const ImVec2 tip(centre.x + static_cast<float>(p * sx) * cell * k,
                         centre.y - static_cast<float>(q * sy) * cell * k);
        dl->AddLine(centre, tip, u32(withAlpha(theme.ember, 0.85f)), 2.2f);
        dl->AddCircleFilled(tip, cell * 0.16f, u32(theme.ember), 10);
      }
    }
  }
  icon(dl, iconStyle, Archetype::Wedge, centre, cell * 1.9f,
       u32(theme.light ? theme.bone : theme.whitePiece),
       u32(theme.light ? theme.panel : theme.ink));
}

/// The pastel set the field drifts through. Deliberately not from the theme: these are
/// the only colours in the shell that mean nothing at all, and giving them names in the
/// palette would invite something to start using them for state.
constexpr view::Rgba kPastels[]{
    view::Rgba::hex(0xC8D6F7), view::Rgba::hex(0xE2D3F7), view::Rgba::hex(0xC9EDE3),
    view::Rgba::hex(0xFAE2CD), view::Rgba::hex(0xF6D2DF), view::Rgba::hex(0xCDEBF7),
};
constexpr int kPastelCount = 6;
constexpr float kZNear = 0.34f;
constexpr float kZFar = 4.6f;

}  // namespace

Deco decoForScreen(int screen) noexcept {
  switch (static_cast<app::Screen>(screen)) {
    case app::Screen::MainMenu:
      return Deco::Manifold;
    case app::Screen::NewGame:
      return Deco::Lattice;
    case app::Screen::Settings:
      return Deco::Tesseract;
    case app::Screen::Editor:
      return Deco::Atom;
    default:
      return Deco::None;
  }
}

DepthField::DepthField() {
  std::uint32_t seed = 0xC0FFEEu;
  bodies_.resize(30);
  for (std::size_t i = 0; i < bodies_.size(); ++i) {
    Body& b = bodies_[i];
    // A few of them are pieces rather than polygons: the background is made out of the
    // game, which is cheaper than inventing a second vocabulary for it.
    b.piece = i % 4 == 3;
    respawn(b, seed, false);
    b.z = kZNear + nextFloat(seed) * (kZFar - kZNear);
  }
}

void DepthField::respawn(Body& b, std::uint32_t& seed, bool nearPlane) const {
  // Born on an annulus, so nothing is ever in front of the menu.
  b.angle = nextFloat(seed) * 2.0f * kPi;
  b.radius = 0.62f + nextFloat(seed) * 1.15f;
  b.angleV = (nextFloat(seed) < 0.5f ? -1.0f : 1.0f) * (0.006f + nextFloat(seed) * 0.013f);
  b.radiusV = 0.02f + nextFloat(seed) * 0.05f;
  b.radiusPhase = nextFloat(seed) * 2.0f * kPi;
  b.spin = nextFloat(seed) * 2.0f * kPi;
  b.spinV = (nextFloat(seed) - 0.5f) * 0.10f;
  b.size = b.piece ? 150.0f + nextFloat(seed) * 190.0f : 90.0f + nextFloat(seed) * 230.0f;
  b.zDrift = -0.010f - nextFloat(seed) * 0.025f;
  b.alphaPhase = nextFloat(seed) * 2.0f * kPi;
  b.alphaV = 0.18f + nextFloat(seed) * 0.24f;
  b.huePhase = nextFloat(seed) * 2.0f * kPi;
  b.hueV = 0.05f + nextFloat(seed) * 0.07f;
  b.colorA = static_cast<std::uint8_t>(nextFloat(seed) * kPastelCount) % kPastelCount;
  b.colorB = static_cast<std::uint8_t>(nextFloat(seed) * kPastelCount) % kPastelCount;
  b.shape = static_cast<std::uint8_t>(nextFloat(seed) * 7.0f) % 7;
  b.z = nearPlane ? kZNear + 0.05f : kZFar;
}

void DepthField::advance(float dt) {
  clock_ += dt;
  // An impulse, not a speed: the field answers a menu move and then settles.
  velocity_ *= std::pow(0.055f, dt);
  if (std::abs(velocity_) < 0.002f) velocity_ = 0.0f;

  std::uint32_t seed = static_cast<std::uint32_t>(clock_ * 1000.0f) | 1u;
  for (Body& b : bodies_) {
    b.angle += b.angleV * dt;
    b.spin += b.spinV * dt;
    b.z += (b.zDrift + velocity_ * (b.piece ? 0.72f : 1.0f)) * dt;
    if (b.z < kZNear) respawn(b, seed, false);
    if (b.z > kZFar + 0.6f) respawn(b, seed, true);
    const float breathe = std::sin(clock_ * b.radiusV + b.radiusPhase) * 0.10f;
    b.px = std::cos(b.angle) * (b.radius + breathe);
    b.py = std::sin(b.angle) * (b.radius + breathe) * 0.92f;
  }
}

void DepthField::draw(ImDrawList* dl, ImVec2 min, ImVec2 max, const view::Theme& theme,
                      IconStyle iconStyle, bool withPieces, bool withPolygons) const {
  (void)theme;
  const ImVec2 centre((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
  const float rx = (max.x - min.x) * 0.46f;
  const float ry = (max.y - min.y) * 0.52f;

  // Painter's order, so something rushing past the camera passes in front of the rest.
  std::vector<const Body*> order;
  order.reserve(bodies_.size());
  for (const Body& b : bodies_) {
    if (b.piece ? withPieces : withPolygons) order.push_back(&b);
  }
  std::sort(order.begin(), order.end(),
            [](const Body* a, const Body* b) { return a->z > b->z; });

  for (const Body* body : order) {
    const Body& b = *body;
    const float k = 1.0f / b.z;
    const ImVec2 at(centre.x + b.px * rx * k * 2.1f, centre.y + b.py * ry * k * 2.1f);
    const float scale = k * 1.35f;
    // Fade in from the far plane and out as it sweeps past, so nothing ever pops.
    const float fade = std::clamp((kZFar + 0.4f - b.z) / 1.1f, 0.0f, 1.0f) *
                       std::clamp((b.z - kZNear) / 0.5f, 0.0f, 1.0f);
    if (fade <= 0.0f) continue;

    const view::Rgba a = kPastels[b.colorA];
    const view::Rgba c = kPastels[b.colorB];
    const float mixT = std::sin(clock_ * b.hueV + b.huePhase) * 0.5f + 0.5f;
    view::Rgba tint{a.r + (c.r - a.r) * mixT, a.g + (c.g - a.g) * mixT,
                    a.b + (c.b - a.b) * mixT, 1.0f};

    if (b.piece) {
      icon(dl, iconStyle, kPieceShapes[b.shape], at, b.size * scale,
           u32(withAlpha(tint, fade * 0.34f)), u32(withAlpha(tint, fade * 0.22f)));
      continue;
    }

    const float alpha =
        fade * (0.20f + 0.26f * (std::sin(clock_ * b.alphaV + b.alphaPhase) * 0.5f + 0.5f));
    const int sides = 3 + static_cast<int>(b.shape % 4);
    dl->PathClear();
    for (int i = 0; i < sides; ++i) {
      const float ang =
          b.spin + 2.0f * kPi * static_cast<float>(i) / static_cast<float>(sides);
      const float r = b.size * scale * (0.55f + 0.35f * std::sin(ang * 3.0f + b.huePhase));
      dl->PathLineTo(ImVec2(at.x + std::cos(ang) * r, at.y + std::sin(ang) * r));
    }
    dl->PathFillConvex(u32(withAlpha(tint, alpha)));
  }
}

void drawDeco(ImDrawList* dl, Deco what, ImVec2 min, ImVec2 max, const view::Theme& theme,
              IconStyle iconStyle, float time, const VariantSpec* variant) {
  switch (what) {
    case Deco::Manifold:
      drawManifold(dl, min, max, theme, iconStyle, time);
      break;
    case Deco::Lattice:
      drawLattice(dl, min, max, theme, iconStyle, time, variant);
      break;
    case Deco::Tesseract:
      drawTesseract(dl, min, max, theme, time);
      break;
    case Deco::Atom:
      drawAtom(dl, min, max, theme, iconStyle, time);
      break;
    case Deco::None:
      break;
  }
}

}  // namespace cb::render
