// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace cb {

/// Bump allocator with LIFO reset, for per-search scratch. Allocation happens
/// once, up front; the hot paths only bump a pointer, so the no-allocation
/// invariant for movegen (AGENTS.md rule 6) is satisfiable by construction.
class Arena {
 public:
  explicit Arena(std::size_t bytes)
      : buf_(static_cast<std::byte*>(::operator new(bytes))), cap_(bytes) {}
  ~Arena() { ::operator delete(buf_); }
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  template <class T>
  [[nodiscard]] T* allocate(std::size_t n) {
    const std::size_t align = alignof(T);
    const std::size_t pad = (align - (used_ % align)) % align;
    const std::size_t need = pad + n * sizeof(T);
    assert(used_ + need <= cap_ && "Arena exhausted: raise the reservation");
    auto* p = reinterpret_cast<T*>(buf_ + used_ + pad);
    used_ += need;
    if (used_ > high_) high_ = used_;
    return p;
  }

  [[nodiscard]] std::size_t mark() const noexcept { return used_; }
  void release(std::size_t m) noexcept {
    assert(m <= used_);
    used_ = m;
  }
  void reset() noexcept { used_ = 0; }

  [[nodiscard]] std::size_t used() const noexcept { return used_; }
  [[nodiscard]] std::size_t capacity() const noexcept { return cap_; }
  /// High-water mark, so reservations can be tuned from real runs.
  [[nodiscard]] std::size_t highWater() const noexcept { return high_; }

 private:
  std::byte* buf_{nullptr};
  std::size_t cap_{0};
  std::size_t used_{0};
  std::size_t high_{0};
};

/// RAII scope that rewinds an arena on exit.
class ScratchArena {
 public:
  explicit ScratchArena(Arena& a) : a_(a), mark_(a.mark()) {}
  ~ScratchArena() { a_.release(mark_); }
  ScratchArena(const ScratchArena&) = delete;
  ScratchArena& operator=(const ScratchArena&) = delete;
  Arena& arena() noexcept { return a_; }

 private:
  Arena& a_;
  std::size_t mark_;
};

}  // namespace cb
