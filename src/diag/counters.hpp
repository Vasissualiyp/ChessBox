// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

/// Deterministic counters for profiling generalized code paths. Compiled out
/// entirely unless CB_DIAG is defined, so release builds pay nothing (asserted
/// by tests/arch).
namespace cb::diag {

#ifdef CB_DIAG
enum class Counter : std::uint8_t {
  InteriorSteps,
  BoundarySteps,
  RaysTerminated,
  MovesGenerated,
  LegalityChecks,
  Count_
};
void bump(Counter c, std::uint64_t n = 1) noexcept;
[[nodiscard]] std::uint64_t value(Counter c) noexcept;
void resetAll() noexcept;
#define CB_COUNT(c) ::cb::diag::bump(::cb::diag::Counter::c)
#define CB_COUNT_N(c, n) ::cb::diag::bump(::cb::diag::Counter::c, (n))
#else
#define CB_COUNT(c) ((void)0)
#define CB_COUNT_N(c, n) ((void)0)
#endif

}  // namespace cb::diag
