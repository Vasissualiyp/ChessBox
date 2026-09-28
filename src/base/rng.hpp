// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace cb {

/// xoshiro256++. Deterministic and identical across compilers and platforms -
/// a requirement, not a convenience: tests, replays, and later self-play all
/// depend on reproducible streams (ARCH section 11).
class Rng {
 public:
  explicit constexpr Rng(std::uint64_t seed) noexcept {
    // SplitMix64 to spread a single seed over the 256-bit state.
    for (std::uint64_t& s : s_) {
      seed += 0x9E3779B97F4A7C15ULL;
      std::uint64_t z = seed;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
      s = z ^ (z >> 31);
    }
  }

  constexpr std::uint64_t next() noexcept {
    const std::uint64_t result = rotl(s_[0] + s_[3], 23) + s_[0];
    const std::uint64_t t = s_[1] << 17;
    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];
    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);
    return result;
  }

  /// Uniform in [0, n) via Lemire's method; unbiased and division-free.
  constexpr std::uint32_t below(std::uint32_t n) noexcept {
    const auto x = static_cast<std::uint64_t>(static_cast<std::uint32_t>(next()));
    return static_cast<std::uint32_t>((x * n) >> 32);
  }
  constexpr bool coin() noexcept { return (next() >> 63) != 0; }

 private:
  static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
  }
  std::uint64_t s_[4]{};
};

}  // namespace cb
