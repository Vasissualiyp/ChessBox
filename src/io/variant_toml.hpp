// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string>

#include "base/result.hpp"
#include "variant/variant.hpp"

namespace cb {

/// Load a variant from its declarative TOML definition.
///
/// This is the only place in the project that reads variant text. Everything
/// below L9 sees a finalized VariantSpec, which is why a Workshop variant can
/// never reach the engine as anything but data (ADR-0005).
///
/// Errors carry the source line, because the first audience for a validation
/// message is a variant author who is not a programmer.
Result<VariantSpec> loadVariantToml(std::string_view text,
                                    std::string_view sourceName = "<memory>");
Result<VariantSpec> loadVariantFile(const std::filesystem::path& path);

/// Parse a FEN-style board section (the first FEN field only) into starting
/// placements. Usable before finalize(), which the full FEN parser is not.
Result<std::vector<StartPiece>> parseBoardSection(const DimSpec& dims,
                                                  const std::vector<PieceTypeDef>& pieces,
                                                  std::string_view board);

}  // namespace cb
