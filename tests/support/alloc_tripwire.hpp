// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace cb::test {

/// Global allocation counters, fed by the replaced operator new/delete in
/// alloc_tripwire.cpp.
std::size_t allocationCount() noexcept;
std::size_t liveBytes() noexcept;

/// Fails loudly if any heap allocation happens inside its scope. This is how the
/// "no allocation in movegen" invariant (AGENTS.md rule 6) is actually enforced
/// rather than merely intended. A tripwire that never trips is worse than none,
/// so it has its own self-test.
class NoAllocScope {
 public:
  NoAllocScope() : before_(allocationCount()) {}
  [[nodiscard]] std::size_t allocations() const noexcept {
    return allocationCount() - before_;
  }
  [[nodiscard]] bool clean() const noexcept { return allocations() == 0; }

 private:
  std::size_t before_;
};

}  // namespace cb::test
