// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <cassert>
#include <cstdint>
#include <vector>

namespace cb {

/// A dense bitset over an arbitrary number of cells, allocated once by its
/// owner. Occupancy lives here; for an 8x8 board this is a single word, so the
/// classical bitboard is the degenerate case of the general engine rather than a
/// separate code path (ARCH section 5).
class BitWords {
 public:
  using Word = std::uint64_t;
  static constexpr std::size_t kBits = 64;

  BitWords() = default;
  explicit BitWords(std::size_t nbits)
      : words_((nbits + kBits - 1) / kBits, 0), nbits_(nbits) {}

  void reset(std::size_t nbits) {
    words_.assign((nbits + kBits - 1) / kBits, 0);
    nbits_ = nbits;
  }
  void clear() noexcept {
    for (Word& w : words_) w = 0;
  }

  [[nodiscard]] std::size_t size() const noexcept { return nbits_; }
  [[nodiscard]] std::size_t wordCount() const noexcept { return words_.size(); }

  void set(std::size_t i) noexcept {
    assert(i < nbits_);
    words_[i / kBits] |= Word{1} << (i % kBits);
  }
  void clearBit(std::size_t i) noexcept {
    assert(i < nbits_);
    words_[i / kBits] &= ~(Word{1} << (i % kBits));
  }
  [[nodiscard]] bool test(std::size_t i) const noexcept {
    assert(i < nbits_);
    return (words_[i / kBits] >> (i % kBits)) & Word{1};
  }

  [[nodiscard]] std::size_t count() const noexcept {
    std::size_t n = 0;
    for (Word w : words_) n += static_cast<std::size_t>(std::popcount(w));
    return n;
  }
  [[nodiscard]] bool none() const noexcept {
    for (Word w : words_) {
      if (w != 0) return false;
    }
    return true;
  }

  /// Visit every set bit in ascending index order. Word-at-a-time, so iterating
  /// pieces costs one popcount-style loop per occupied word.
  template <class F>
  void forEach(F&& f) const {
    for (std::size_t wi = 0; wi < words_.size(); ++wi) {
      Word w = words_[wi];
      while (w != 0) {
        const auto b = static_cast<std::size_t>(std::countr_zero(w));
        w &= w - 1;
        f(wi * kBits + b);
      }
    }
  }

  friend bool operator==(const BitWords& a, const BitWords& b) {
    return a.nbits_ == b.nbits_ && a.words_ == b.words_;
  }

 private:
  std::vector<Word> words_;
  std::size_t nbits_{0};
};

}  // namespace cb
