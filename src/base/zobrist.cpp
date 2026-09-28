// SPDX-License-Identifier: GPL-3.0-or-later
#include "base/zobrist.hpp"

namespace cb {

void Zobrist::init(std::uint64_t seed, std::uint32_t cellCount, std::uint32_t pieceCodes,
                   std::uint32_t extraSlots) {
  Rng rng(seed);
  pieceCodes_ = pieceCodes;
  pieceKeys_.resize(static_cast<std::size_t>(cellCount) * pieceCodes);
  for (std::uint64_t& k : pieceKeys_) k = rng.next();
  epKeys_.resize(static_cast<std::size_t>(cellCount) + 1);
  for (std::uint64_t& k : epKeys_) k = rng.next();
  epKeys_[cellCount] = 0;  // "no ep target" must contribute nothing
  castleKeys_.resize(256);
  for (std::uint64_t& k : castleKeys_) k = rng.next();
  castleKeys_[0] = 0;  // "no rights" must contribute nothing
  extraKeys_.resize(extraSlots);
  for (std::uint64_t& k : extraKeys_) k = rng.next();
  sideKey_ = rng.next();
}

}  // namespace cb
