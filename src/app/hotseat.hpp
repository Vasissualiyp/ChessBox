// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "pieces/atom.hpp"
#include "space/coord.hpp"
#include "space/dim_spec.hpp"
#include "space/dims.hpp"

namespace cb::app {

/// Two players sharing one keyboard, one half each.
///
/// Each half is an ordered list of keys; a key's position in its list is the coordinate
/// it enters, so it works on any board without a per-variant cheat sheet. A square is
/// entered one coordinate per board axis - two keys on a flat board, `qq` for a1 - and
/// the half's cancel key clears a half-typed square. Only the player to move can enter;
/// the other half is ignored, so a stray key cannot play the opponent's move.
class HotSeat {
 public:
  /// Player one's keys, left to right, top to bottom, then player two's.
  static constexpr std::string_view whiteKeys() { return "qwertasdfgzxcvb"; }
  static constexpr std::string_view blackKeys() { return "yuiophjkl;nm,./"; }
  static constexpr char whiteCancel() { return '\t'; }  // Tab
  static constexpr char blackCancel() { return '\\'; }

  /// Which half a key belongs to, or nullopt.
  [[nodiscard]] static std::optional<Color> ownerOf(char key);

  /// Feed one key, as the unshifted character it produces. Returns the cell a completed
  /// square names - the caller clicks it - or nullopt while a square is still being
  /// entered or the key was ignored.
  [[nodiscard]] std::optional<CellId> feed(char key, const DimSpec& dims, Color toMove);

  /// The coordinates typed so far by a player, for the on-screen readout. Empty when
  /// nothing is half-entered.
  [[nodiscard]] std::vector<std::int16_t> partial(Color who) const;

 private:
  struct Entry {
    int axis{0};
    std::array<std::int16_t, kMaxDims> coords{};
  };
  [[nodiscard]] Entry& entryFor(Color who) {
    return who == Color::White ? white_ : black_;
  }
  [[nodiscard]] const Entry& entryFor(Color who) const {
    return who == Color::White ? white_ : black_;
  }

  Entry white_;
  Entry black_;
};

}  // namespace cb::app
