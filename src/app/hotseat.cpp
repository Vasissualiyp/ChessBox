// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hotseat.hpp"

namespace cb::app {
namespace {

int indexOf(std::string_view keys, char key) {
  for (std::size_t i = 0; i < keys.size(); ++i) {
    if (keys[i] == key) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

std::optional<Color> HotSeat::ownerOf(char key) {
  if (indexOf(whiteKeys(), key) >= 0) return Color::White;
  if (indexOf(blackKeys(), key) >= 0) return Color::Black;
  return std::nullopt;
}

std::optional<CellId> HotSeat::feed(char key, const DimSpec& dims, Color toMove) {
  if (key == whiteCancel()) {
    white_ = Entry{};
    return std::nullopt;
  }
  if (key == blackCancel()) {
    black_ = Entry{};
    return std::nullopt;
  }

  const auto owner = ownerOf(key);
  if (!owner.has_value()) return std::nullopt;
  // Not this player's move: ignore the key rather than refuse loudly, so an unused
  // hand resting on the keys cannot disturb the player who is thinking.
  if (*owner != toMove) return std::nullopt;

  Entry& entry = entryFor(*owner);
  if (entry.axis >= dims.dims()) entry.axis = 0;  // defensive; feed resets on completion
  const std::string_view keys = *owner == Color::White ? whiteKeys() : blackKeys();
  const int index = indexOf(keys, key);
  const auto axis = static_cast<std::uint8_t>(entry.axis);
  if (index < 0 || index >= dims.extent(axis)) {
    return std::nullopt;  // a key past this board's edge
  }
  entry.coords[static_cast<std::size_t>(axis)] = static_cast<std::int16_t>(index);
  ++entry.axis;

  if (entry.axis < dims.dims()) return std::nullopt;

  Coord coord(dims.dims());
  for (std::uint8_t a = 0; a < dims.dims(); ++a) {
    coord.c[a] = entry.coords[a];
  }
  entry = Entry{};
  return dims.toCell(coord);
}

std::vector<std::int16_t> HotSeat::partial(Color who) const {
  const Entry& entry = entryFor(who);
  return {entry.coords.begin(), entry.coords.begin() + entry.axis};
}

}  // namespace cb::app
