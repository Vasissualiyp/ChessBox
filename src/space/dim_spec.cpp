// SPDX-License-Identifier: GPL-3.0-or-later
#include "space/dim_spec.hpp"

namespace cb {

std::string_view toString(AxisKind k) noexcept {
  switch (k) {
    case AxisKind::Spatial: return "spatial";
    case AxisKind::Temporal: return "temporal";
    case AxisKind::Multiverse: return "multiverse";
  }
  return "?";
}

Result<DimSpec> DimSpec::create(std::span<const AxisDecl> axes) {
  if (axes.empty()) {
    return fail(ErrorCode::ValidationError, "a board needs at least one axis");
  }
  if (axes.size() > static_cast<std::size_t>(kMaxDims)) {
    return fail(ErrorCode::BudgetExceeded,
                "board has " + std::to_string(axes.size()) + " axes but this build supports " +
                    std::to_string(kMaxDims) + " (see kMaxDims in src/space/dims.hpp)");
  }

  DimSpec out;
  out.n_ = static_cast<std::uint8_t>(axes.size());

  // Accumulate in 64 bits so an absurd extent product is a validation error
  // rather than a silent wrap followed by a bad_alloc.
  std::uint64_t total = 1;
  for (std::size_t i = 0; i < axes.size(); ++i) {
    const AxisDecl& a = axes[i];
    if (a.extent <= 0) {
      return fail(ErrorCode::ValidationError,
                  "axis '" + a.name + "' has non-positive extent " + std::to_string(a.extent));
    }
    if (a.extent > 0x7FFF) {
      return fail(ErrorCode::ValidationError,
                  "axis '" + a.name + "' extent " + std::to_string(a.extent) +
                      " exceeds the 32767 coordinate limit");
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (!axes[j].name.empty() && axes[j].name == a.name) {
        return fail(ErrorCode::ValidationError, "duplicate axis name '" + a.name + "'");
      }
    }
    out.extent_[i] = static_cast<std::int16_t>(a.extent);
    out.kind_[i] = a.kind;
    out.names_[i] = a.name;
    total *= static_cast<std::uint64_t>(a.extent);
    if (total > kMaxCells) {
      return fail(ErrorCode::BudgetExceeded,
                  "board would have more than " + std::to_string(kMaxCells) +
                      " cells; reduce extents or dimension count");
    }
  }

  std::uint32_t s = 1;
  for (std::size_t i = 0; i < axes.size(); ++i) {
    out.stride_[i] = s;
    s *= static_cast<std::uint32_t>(out.extent_[i]);
  }
  out.cellCount_ = s;
  return out;
}

int DimSpec::axisIndex(std::string_view name) const {
  for (std::uint8_t i = 0; i < n_; ++i) {
    if (names_[i] == name) return i;
  }
  return -1;
}

}  // namespace cb
