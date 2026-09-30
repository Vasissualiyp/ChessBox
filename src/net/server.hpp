// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/result.hpp"
#include "game/game.hpp"
#include "net/transport.hpp"
#include "variant/variant.hpp"

namespace cb::net {

/// The authoritative game server.
///
/// It owns the `Game` and is the only thing that ever advances it: an intent is checked
/// against the engine's own legal moves and, if it is one of them, played; whatever
/// happens, the resulting position is sent back by ply, side and hash.
///
/// Any number of clients share it. Each announces itself with a hello and is told the
/// current position, so a client that joins - or reconnects after a drop - is resynced
/// the same way, with no separate path. A client may predict for responsiveness, but the
/// server's word is final: there is no second rules implementation anywhere.
class Server {
 public:
  /// A server with no clients yet; add them with addClient.
  [[nodiscard]] static Result<std::unique_ptr<Server>> create(VariantSpec variant);
  /// The common case: a server and its first client.
  [[nodiscard]] static Result<std::unique_ptr<Server>> create(VariantSpec variant,
                                                              Transport& transport);

  /// Add a link. It must say hello before any intent is accepted.
  void addClient(Transport& transport);

  /// Give the server the variant's source text, so a client whose hello names a variant
  /// it does not have can be sent it rather than turned away.
  void setVariantSource(std::string source) { source_ = std::move(source); }

  /// Read and answer everything waiting on every link. Returns false when no live link
  /// remains - every peer has been turned away.
  bool poll();

  [[nodiscard]] const Game& game() const noexcept { return game_; }
  [[nodiscard]] std::uint64_t positionHash() const { return game_.position().hash(); }
  [[nodiscard]] std::size_t clientCount() const noexcept { return links_.size(); }

 private:
  explicit Server(VariantSpec variant) : variant_(std::move(variant)), game_(variant_) {}

  struct Link {
    Transport* transport;
    bool helloed{false};
  };

  [[nodiscard]] const Move* findLegal(std::uint32_t from, std::uint32_t to) const;
  void sendState(Transport& to) const;
  void broadcastState() const;
  static void sendRefusal(Transport& to, const std::string& reason);

  VariantSpec variant_;
  std::string source_;
  Game game_;
  std::vector<Link> links_;
};

}  // namespace cb::net
