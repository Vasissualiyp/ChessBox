// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/alloc_tripwire.hpp"

#include <cstdlib>
#include <new>

namespace {
std::size_t g_allocations = 0;
std::size_t g_live = 0;
}  // namespace

namespace cb::test {
std::size_t allocationCount() noexcept { return g_allocations; }
std::size_t liveBytes() noexcept { return g_live; }
}  // namespace cb::test

void* operator new(std::size_t n) {
  ++g_allocations;
  g_live += n;
  void* p = std::malloc(n == 0 ? 1 : n);
  if (p == nullptr) throw std::bad_alloc();
  return p;
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
