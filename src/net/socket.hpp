// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/result.hpp"
#include "net/transport.hpp"

namespace cb::net {

/// A TCP transport.
///
/// Bytes are framed with the protocol's own length prefix and reassembled from whatever
/// the socket delivers, so a frame split across two packets arrives as one receive() and
/// two frames in one packet arrive as two. Non-blocking: receive() never waits.
///
/// No TLS yet: the interface is the same, and a `TlsTransport` wrapping this one is the
/// remaining piece - the protocol above does not know the difference.
class SocketTransport final : public Transport {
 public:
  explicit SocketTransport(int fd) : fd_(fd) {}
  ~SocketTransport() override;

  SocketTransport(const SocketTransport&) = delete;
  SocketTransport& operator=(const SocketTransport&) = delete;

  void send(std::string_view frame) override;
  [[nodiscard]] std::optional<std::string> receive() override;

 private:
  int fd_;
  std::string buffer_;  ///< bytes not yet formed into a whole frame
};

/// A bound, listening socket. Ask for port 0 to let the OS pick a free one.
class SocketListener {
 public:
  static Result<std::unique_ptr<SocketListener>> bind(std::uint16_t port);
  ~SocketListener();

  SocketListener(const SocketListener&) = delete;
  SocketListener& operator=(const SocketListener&) = delete;

  [[nodiscard]] std::uint16_t port() const noexcept { return port_; }
  /// Accept one connection, waiting up to `timeoutMs`. Fails with a timeout error when
  /// none arrives.
  [[nodiscard]] Result<std::unique_ptr<SocketTransport>> accept(int timeoutMs) const;

 private:
  SocketListener(int fd, std::uint16_t port) : fd_(fd), port_(port) {}
  int fd_;
  std::uint16_t port_;
};

/// Connect to `host:port`, waiting up to `timeoutMs`.
[[nodiscard]] Result<std::unique_ptr<SocketTransport>> connectTo(const std::string& host,
                                                                 std::uint16_t port,
                                                                 int timeoutMs);

}  // namespace cb::net
