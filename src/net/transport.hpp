// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cb::net {

/// A channel of framed messages.
///
/// The server and the client are written against this and never against sockets, so the
/// whole protocol - validation, refusal, resync - is exercised in-process, with no
/// network and no timing. The real socket transport is one implementation of this and
/// nothing else changes.
class Transport {
 public:
  Transport() = default;
  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;
  virtual ~Transport() = default;

  virtual void send(std::string_view frame) = 0;
  /// The next frame, or nullopt when none is waiting. Never blocks: the caller polls,
  /// which is what keeps a turn-based game free of timeouts inside the engine.
  [[nodiscard]] virtual std::optional<std::string> receive() = 0;
};

}  // namespace cb::net
