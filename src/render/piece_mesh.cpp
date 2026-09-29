// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/piece_mesh.hpp"

#include <cmath>
#include <numbers>

namespace cb::render {
namespace {

constexpr int kSegments = 14;  // smooth enough at board scale, cheap enough to be free

void pushTri(MeshLibrary& m, std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  m.indices.push_back(static_cast<std::uint16_t>(a));
  m.indices.push_back(static_cast<std::uint16_t>(b));
  m.indices.push_back(static_cast<std::uint16_t>(c));
}

std::uint32_t pushVertex(MeshLibrary& m, float x, float y, float z, float nx, float ny,
                         float nz, float h) {
  const auto index = static_cast<std::uint32_t>(m.vertices.size());
  m.vertices.push_back(MeshVertex{{x, y, z}, {nx, ny, nz}, h});
  return index;
}

/// A truncated cone, which covers every round part of every piece: a disc when the
/// radii match, a spike when the top radius is zero.
void addCone(MeshLibrary& m, std::int32_t base, float z0, float z1, float r0, float r1,
             float totalHeight, bool capTop) {
  const float tau = 2.0f * std::numbers::pi_v<float>;
  const float slope = std::atan2(r0 - r1, z1 - z0);
  const float nz = std::sin(slope);
  const float nr = std::cos(slope);

  for (int i = 0; i < kSegments; ++i) {
    const float a0 = tau * static_cast<float>(i) / kSegments;
    const float a1 = tau * static_cast<float>(i + 1) / kSegments;
    const float c0 = std::cos(a0);
    const float s0 = std::sin(a0);
    const float c1 = std::cos(a1);
    const float s1 = std::sin(a1);

    const std::uint32_t v0 =
        pushVertex(m, c0 * r0, s0 * r0, z0, c0 * nr, s0 * nr, nz, z0 / totalHeight);
    const std::uint32_t v1 =
        pushVertex(m, c1 * r0, s1 * r0, z0, c1 * nr, s1 * nr, nz, z0 / totalHeight);
    const std::uint32_t v2 =
        pushVertex(m, c1 * r1, s1 * r1, z1, c1 * nr, s1 * nr, nz, z1 / totalHeight);
    const std::uint32_t v3 =
        pushVertex(m, c0 * r1, s0 * r1, z1, c0 * nr, s0 * nr, nz, z1 / totalHeight);
    pushTri(m, v0 - static_cast<std::uint32_t>(base),
            v1 - static_cast<std::uint32_t>(base), v2 - static_cast<std::uint32_t>(base));
    pushTri(m, v0 - static_cast<std::uint32_t>(base),
            v2 - static_cast<std::uint32_t>(base), v3 - static_cast<std::uint32_t>(base));
  }

  if (capTop && r1 > 0.0f) {
    const std::uint32_t centre = pushVertex(m, 0, 0, z1, 0, 0, 1, z1 / totalHeight);
    for (int i = 0; i < kSegments; ++i) {
      const float a0 = tau * static_cast<float>(i) / kSegments;
      const float a1 = tau * static_cast<float>(i + 1) / kSegments;
      const std::uint32_t v0 = pushVertex(m, std::cos(a0) * r1, std::sin(a0) * r1, z1, 0,
                                          0, 1, z1 / totalHeight);
      const std::uint32_t v1 = pushVertex(m, std::cos(a1) * r1, std::sin(a1) * r1, z1, 0,
                                          0, 1, z1 / totalHeight);
      pushTri(m, centre - static_cast<std::uint32_t>(base),
              v0 - static_cast<std::uint32_t>(base),
              v1 - static_cast<std::uint32_t>(base));
    }
  }
}

void addBox(MeshLibrary& m, std::int32_t base, float cx, float cy, float cz, float hx,
            float hy, float hz, float totalHeight) {
  const float faces[6][3] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                             {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
  for (const auto& n : faces) {
    float a[3]{0, 0, 0};
    float b[3]{0, 0, 0};
    if (n[0] != 0) {
      a[1] = 1;
      b[2] = 1;
    } else if (n[1] != 0) {
      a[0] = 1;
      b[2] = 1;
    } else {
      a[0] = 1;
      b[1] = 1;
    }
    const float half[3]{hx, hy, hz};
    const float centre[3]{cx, cy, cz};
    const float signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    std::uint32_t quad[4]{};
    for (int k = 0; k < 4; ++k) {
      float p[3];
      for (int axis = 0; axis < 3; ++axis) {
        p[axis] = centre[axis] + n[axis] * half[axis] +
                  a[axis] * signs[k][0] * half[axis] + b[axis] * signs[k][1] * half[axis];
      }
      quad[k] = pushVertex(m, p[0], p[1], p[2], n[0], n[1], n[2], p[2] / totalHeight);
    }
    const auto off = static_cast<std::uint32_t>(base);
    pushTri(m, quad[0] - off, quad[1] - off, quad[2] - off);
    pushTri(m, quad[0] - off, quad[2] - off, quad[3] - off);
  }
}

/// Every piece stands on the same foot, which is what makes an army look like a set.
void addFoot(MeshLibrary& m, std::int32_t base, float totalHeight) {
  addCone(m, base, 0.00f, 0.09f, 0.34f, 0.30f, totalHeight, false);
  addCone(m, base, 0.09f, 0.14f, 0.30f, 0.22f, totalHeight, false);
}

void buildArchetype(MeshLibrary& m, Archetype which, std::int32_t base) {
  switch (which) {
    case Archetype::Cell:
      // The board cell: a thin slab, drawn by the same pipeline as everything else.
      addBox(m, base, 0, 0, 0, 0.46f, 0.46f, 0.055f, 1.0f);
      return;

    case Archetype::Dome: {  // pawn
      const float h = 0.62f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.34f, 0.20f, 0.17f, h, false);
      addCone(m, base, 0.34f, 0.44f, 0.24f, 0.20f, h, false);
      addCone(m, base, 0.44f, 0.62f, 0.20f, 0.02f, h, true);
      return;
    }

    case Archetype::Tower: {  // rook
      const float h = 0.86f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.66f, 0.24f, 0.26f, h, false);
      addCone(m, base, 0.66f, 0.72f, 0.31f, 0.31f, h, true);
      // Crenellations, which are what make a rook unmistakable in silhouette.
      for (int i = 0; i < 4; ++i) {
        const float angle = std::numbers::pi_v<float> * 0.5f * static_cast<float>(i);
        addBox(m, base, std::cos(angle) * 0.20f, std::sin(angle) * 0.20f, 0.80f, 0.09f,
               0.09f, 0.09f, h);
      }
      return;
    }

    case Archetype::Wedge: {  // knight
      const float h = 0.88f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.40f, 0.22f, 0.19f, h, false);
      // A head leaning forward: asymmetry is the whole point, so a knight can be told
      // from a bishop at a glance and from any angle.
      addBox(m, base, 0.00f, 0.02f, 0.56f, 0.15f, 0.20f, 0.17f, h);
      addBox(m, base, 0.00f, 0.16f, 0.72f, 0.12f, 0.16f, 0.12f, h);
      addBox(m, base, 0.00f, -0.10f, 0.78f, 0.07f, 0.09f, 0.10f, h);
      return;
    }

    case Archetype::Spire: {  // bishop
      const float h = 0.96f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.30f, 0.21f, 0.18f, h, false);
      addCone(m, base, 0.30f, 0.36f, 0.26f, 0.22f, h, false);
      addCone(m, base, 0.36f, 0.82f, 0.20f, 0.05f, h, false);
      addCone(m, base, 0.82f, 0.96f, 0.07f, 0.01f, h, true);
      return;
    }

    case Archetype::Crown: {  // queen
      const float h = 1.05f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.62f, 0.23f, 0.17f, h, false);
      addCone(m, base, 0.62f, 0.72f, 0.30f, 0.26f, h, false);
      for (int i = 0; i < 6; ++i) {
        const float angle =
            2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / 6.0f;
        addBox(m, base, std::cos(angle) * 0.19f, std::sin(angle) * 0.19f, 0.86f, 0.055f,
               0.055f, 0.14f, h);
      }
      addCone(m, base, 1.00f, 1.05f, 0.06f, 0.00f, h, true);
      return;
    }

    case Archetype::Monolith: {  // king
      const float h = 1.18f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.70f, 0.24f, 0.19f, h, false);
      addCone(m, base, 0.70f, 0.80f, 0.29f, 0.25f, h, false);
      // A cross: the one silhouette nobody mistakes for anything else.
      addBox(m, base, 0, 0, 0.99f, 0.06f, 0.06f, 0.19f, h);
      addBox(m, base, 0, 0, 1.02f, 0.16f, 0.06f, 0.06f, h);
      return;
    }

    case Archetype::Horn: {  // unicorn and friends
      const float h = 1.00f;
      addFoot(m, base, h);
      addCone(m, base, 0.14f, 0.34f, 0.22f, 0.19f, h, false);
      // A long spike leaning off-axis, so a piece that only exists in three or more
      // dimensions looks like it belongs to a different geometry than the rest.
      addBox(m, base, 0.05f, 0.00f, 0.52f, 0.13f, 0.13f, 0.20f, h);
      addCone(m, base, 0.72f, 1.00f, 0.11f, 0.01f, h, true);
      return;
    }

    case Archetype::Count:
      return;
  }
}

}  // namespace

Archetype archetypeFromName(std::string_view name) {
  if (name == "dome") return Archetype::Dome;
  if (name == "tower") return Archetype::Tower;
  if (name == "wedge") return Archetype::Wedge;
  if (name == "spire") return Archetype::Spire;
  if (name == "crown") return Archetype::Crown;
  if (name == "monolith") return Archetype::Monolith;
  if (name == "horn") return Archetype::Horn;
  return Archetype::Tower;
}

std::string_view archetypeName(Archetype a) {
  switch (a) {
    case Archetype::Cell:
      return "cell";
    case Archetype::Dome:
      return "dome";
    case Archetype::Tower:
      return "tower";
    case Archetype::Wedge:
      return "wedge";
    case Archetype::Spire:
      return "spire";
    case Archetype::Crown:
      return "crown";
    case Archetype::Monolith:
      return "monolith";
    case Archetype::Horn:
      return "horn";
    case Archetype::Count:
      break;
  }
  return "tower";
}

Archetype archetypeFor(const PieceTypeDef& piece) {
  if (!piece.shape.empty()) return archetypeFromName(piece.shape);
  if (piece.royal) return Archetype::Monolith;

  // Read the movement. Order of tests matters: the more specific a piece's movement is,
  // the earlier it should be recognised.
  bool slides = false;
  bool leaps = false;
  bool oriented = false;
  std::size_t maxOrder = 0;
  std::size_t maxDiagonalOrder = 0;
  for (const MoveAtom& a : piece.atoms) {
    maxOrder = std::max(maxOrder, a.mags.size());
    if (a.oriented) oriented = true;
    if (a.mode == MoveMode::Slide && a.maxK > 1) slides = true;
    if (a.mode == MoveMode::Leap || a.maxK == 1) leaps = true;
    bool uniform = true;
    for (std::size_t i = 1; i < a.mags.size(); ++i) {
      if (a.mags[i] != a.mags[0]) uniform = false;
    }
    if (uniform && a.mags.size() >= 2)
      maxDiagonalOrder = std::max(maxDiagonalOrder, a.mags.size());
  }

  if (oriented) return Archetype::Dome;                   // moves only forward: a pawn
  if (maxDiagonalOrder >= 3) return Archetype::Horn;      // needs three axes at once
  if (!slides && maxOrder >= 2) return Archetype::Wedge;  // a leaper with an offset
  if (slides && piece.atoms.size() >= 2)
    return Archetype::Crown;  // several sliding families
  if (slides && maxDiagonalOrder == 2) return Archetype::Spire;  // a diagonal slider
  if (slides) return Archetype::Tower;                           // an orthogonal slider
  (void)leaps;
  return Archetype::Dome;
}

float heightFor(const PieceTypeDef& piece) {
  if (piece.heightPermille > 0) return static_cast<float>(piece.heightPermille) / 1000.0f;

  // No declaration: infer from how far the piece can go. Unlimited sliders stand tall,
  // single-step pieces stay low, so an unfamiliar army still has a readable hierarchy.
  float tallest = 0.8f;
  for (const MoveAtom& a : piece.atoms) {
    const float reach =
        a.maxK == kUnlimited ? 1.0f : std::min(1.0f, static_cast<float>(a.maxK) / 4.0f);
    tallest = std::max(tallest, 0.8f + 0.35f * reach);
  }
  if (piece.royal) tallest = std::max(tallest, 1.25f);
  return tallest;
}

MeshLibrary MeshLibrary::build() {
  MeshLibrary m;
  for (std::size_t i = 0; i < static_cast<std::size_t>(Archetype::Count); ++i) {
    const auto base = static_cast<std::int32_t>(m.vertices.size());
    MeshRange range;
    range.firstIndex = static_cast<std::uint32_t>(m.indices.size());
    range.vertexOffset = base;
    buildArchetype(m, static_cast<Archetype>(i), base);
    range.indexCount = static_cast<std::uint32_t>(m.indices.size()) - range.firstIndex;
    m.ranges[i] = range;
  }
  return m;
}

}  // namespace cb::render
