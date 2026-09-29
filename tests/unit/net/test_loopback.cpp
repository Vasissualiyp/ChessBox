// SPDX-License-Identifier: GPL-3.0-or-later
//
// M8.4: the in-process transport. The protocol rides on it in tests, so a framing bug is
// a unit-test failure rather than something only a socket would show.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "net/loopback.hpp"
#include "net/protocol.hpp"

using namespace cb;
using namespace cb::net;

TEST_CASE("a loopback carries frames both ways", "[unit][net]") {
  Loopback l = makeLoopback();
  CHECK_FALSE(l.a->receive().has_value());
  CHECK_FALSE(l.b->receive().has_value());

  l.a->send("ping");
  const auto at_b = l.b->receive();
  REQUIRE(at_b.has_value());
  CHECK(*at_b == "ping");
  CHECK_FALSE(l.b->receive().has_value());  // drained

  l.b->send("pong");
  const auto at_a = l.a->receive();
  REQUIRE(at_a.has_value());
  CHECK(*at_a == "pong");
  CHECK_FALSE(l.a->receive().has_value());
}

TEST_CASE("a message sent over a loopback decodes on the far side", "[unit][net]") {
  Loopback l = makeLoopback();
  Message hello;
  hello.kind = MessageKind::Hello;
  hello.variantHash = 0x5A;
  l.a->send(encodeFrame(hello));

  const auto frame = l.b->receive();
  REQUIRE(frame.has_value());
  const auto decoded = decodeFrame(*frame);
  REQUIRE(decoded.has_value());
  CHECK(decoded->kind == MessageKind::Hello);
  CHECK(decoded->variantHash == 0x5A);
  CHECK(acceptsHello(*decoded, 0x5A));
}
