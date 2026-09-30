// SPDX-License-Identifier: GPL-3.0-or-later
//
// M8.1: a frame over a real TCP socket. The socket is the same Transport, so the tests
// above are the protocol's tests and this only proves the transport itself: a length
// prefix is reassembled across whatever the socket delivers.
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include "net/protocol.hpp"
#include "net/socket.hpp"

using namespace cb;
using namespace cb::net;

TEST_CASE("a frame crosses a real TCP socket", "[unit][net]") {
  auto listener = SocketListener::bind(0);
  if (!listener.has_value()) SKIP("no sockets: " + listener.error().format());

  auto client = connectTo("127.0.0.1", (*listener)->port(), 2000);
  if (!client.has_value()) SKIP("cannot connect: " + client.error().format());

  auto server = (*listener)->accept(2000);
  if (!server.has_value()) SKIP("cannot accept: " + server.error().format());

  Message hello;
  hello.kind = MessageKind::Hello;
  hello.variantHash = 0x99;
  (*client)->send(encodeFrame(hello));

  std::optional<std::string> frame;
  for (int i = 0; i < 500 && !frame.has_value(); ++i) {
    frame = (*server)->receive();
    if (!frame.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  }
  REQUIRE(frame.has_value());
  const auto decoded = decodeFrame(*frame);
  REQUIRE(decoded.has_value());
  CHECK(decoded->kind == MessageKind::Hello);
  CHECK(decoded->variantHash == 0x99);
}
