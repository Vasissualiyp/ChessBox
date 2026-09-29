// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "net/transport.hpp"

namespace cb::net {

/// Two transports wired to each other: what one side sends, the other receives. The
/// in-process transport the tests and a local hot-seat game use, so a protocol bug shows
/// up in a unit test rather than behind a socket.
struct Loopback {
  std::unique_ptr<Transport> a;
  std::unique_ptr<Transport> b;
};

[[nodiscard]] Loopback makeLoopback();

}  // namespace cb::net
