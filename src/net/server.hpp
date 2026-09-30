// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>

#include "base/result.hpp"
#include "game/game.hpp"
#include "net/transport.hpp"
#include "variant/variant.hpp"

namespace cb::net {

/// The authoritative game server.
///
/// It owns the `Game` and is the only thing that ever advances it: an intent is checked
/// against the engine's own legal moves and, if it is one of them, played; whatever
/// happens, the resulting position is sent back by ply, side and hash. A client may
/// predict for responsiveness, but the server's word is final - there is no second rules
/// implementation anywhere, and no client ever advances the state on its own.
class Server {
 public:
  /// Takes the variant by value and keeps it at a stable address: the Game holds a
  /// pointer into it, so the Server must never move once built - hence the unique_ptr.
  [[nodiscard]] static Result<std::unique_ptr<Server>> create(VariantSpec variant,
                                                              Transport& transport);

  /// Read and answer everything waiting on the transport. Returns false when the peer
  /// has been turned away (a bad hello).
  bool poll();

  [[nodiscard]] const Game& game() const noexcept { return game_; }
  [[nodiscard]] std::uint64_t positionHash() const { return game_.position().hash(); }

 private:
  Server(VariantSpec variant, Transport& transport)
      : variant_(std::move(variant)), game_(variant_), transport_(&transport) {}

  void sendState();
  void sendRefusal(const std::string& reason);

  VariantSpec variant_;
  Game game_;
  Transport* transport_;
  bool helloed_{false};
};

}  // namespace cb::net
