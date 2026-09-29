// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string>

#include "io/variant_toml.hpp"
#include "variant/variant.hpp"

namespace cb::test {

inline std::filesystem::path repoRoot() {
  return std::filesystem::path{CB_SOURCE_DIR};
}
inline std::filesystem::path variantPath(std::string_view name) {
  return repoRoot() / "variants" / (std::string(name) + ".toml");
}

inline VariantSpec loadVariant(std::string_view name) {
  auto v = loadVariantFile(variantPath(name));
  if (!v.has_value()) throw std::runtime_error(v.error().format());
  return std::move(*v);
}

}  // namespace cb::test
