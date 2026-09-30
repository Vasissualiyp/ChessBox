// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "base/result.hpp"
#include "net/transport.hpp"
#include "space/coord.hpp"

namespace cb::net {

/// The thin client: it sends intents and mirrors the server's state. It holds no rules
/// and never advances the game itself - the hash it carries is the server's, which is
/// what lets a mismatch be caught loudly rather than diverging in silence.
class Client {
 public:
  /// Announce ourselves with the variant we expect. A mismatch is the server's to
  /// refuse, and it will.
  [[nodiscard]] static Result<std::unique_ptr<Client>> create(Transport& transport,
                                                              std::uint64_t variantHash);

  void sendIntent(CellId from, CellId to);

  /// Read everything the server has sent. Returns false when the server refused us.
  bool poll();

  [[nodiscard]] bool synced() const noexcept { return synced_; }
  [[nodiscard]] std::uint32_t ply() const noexcept { return ply_; }
  [[nodiscard]] std::uint64_t positionHash() const noexcept { return hash_; }
  [[nodiscard]] const std::string& lastRefusal() const noexcept { return refusal_; }
  /// The variant's source, if the server sent one because we did not have it.
  [[nodiscard]] const std::string& variantSource() const noexcept {
    return variantSource_;
  }

 private:
  explicit Client(Transport& transport) : transport_(&transport) {}

  Transport* transport_;
  std::uint32_t ply_{0};
  std::uint64_t hash_{0};
  std::string refusal_;
  std::string variantSource_;
  bool synced_{false};
};

}  // namespace cb::net
