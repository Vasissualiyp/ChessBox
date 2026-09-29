// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "variant/variant.hpp"

namespace cb::render {

struct MeshVertex {
  float pos[3];
  float normal[3];
  /// Position within the piece's own bounding box, 0 at the base and 1 at the top.
  /// The shader uses it to shade a piece from its foot upward, which is what stops a
  /// tall piece reading as a flat slab.
  float height;
};

/// A range inside the shared vertex and index buffers.
struct MeshRange {
  std::uint32_t firstIndex{0};
  std::uint32_t indexCount{0};
  std::int32_t vertexOffset{0};
};

/// The shapes a piece can be given.
///
/// Pieces cannot ship as model files: a variant may declare a piece nobody anticipated,
/// and a Workshop author certainly will. So each piece is assembled at load from
/// primitives - a base, a shaft, a crown - against an archetype the variant names. A
/// user-defined piece gets a body without anyone authoring a mesh.
enum class Archetype : std::uint8_t {
  Cell,      ///< the board cell itself, a flat slab
  Dome,      ///< pawn
  Tower,     ///< rook
  Wedge,     ///< knight
  Spire,     ///< bishop
  Crown,     ///< queen
  Monolith,  ///< king, or anything royal
  Horn,      ///< unicorn and other pieces that only exist above two dimensions
  Portal,    ///< the opening a wrapped move passes through: a cube, so it can stand
             ///< on whichever axis the seam it belongs to actually faces
  Count
};

/// Resolve a name from a variant file. An unknown name is not an error - it falls back
/// to Tower, because a variant must never be unplayable for want of a model.
Archetype archetypeFromName(std::string_view name);
std::string_view archetypeName(Archetype a);

/// Pick a shape for a piece that did not declare one.
///
/// Royal pieces get the monolith. Otherwise the choice follows the piece's *movement*:
/// how many axes its atoms need and how far they reach. That is already the engine's
/// best evidence about what a piece is, so a variant that says nothing still gets an
/// army that reads at a glance rather than seven identical blocks.
Archetype archetypeFor(const PieceTypeDef& piece);

/// Relative height, with a pawn at 1.0. Encodes value, so the tallest piece on the
/// board is the one that matters most even in an army you have never seen.
float heightFor(const PieceTypeDef& piece);

/// Build every archetype into one vertex and index buffer, with a range per shape.
struct MeshLibrary {
  std::vector<MeshVertex> vertices;
  std::vector<std::uint16_t> indices;
  MeshRange ranges[static_cast<std::size_t>(Archetype::Count)];

  static MeshLibrary build();
};

}  // namespace cb::render
