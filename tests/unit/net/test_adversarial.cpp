// SPDX-License-Identifier: GPL-3.0-or-later
//
// M8.4: the adversarial client. Every hostile frame is a named test; none of them may
// crash the server, hang it, or move the game.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "net/client.hpp"
#include "net/loopback.hpp"
#include "net/protocol.hpp"
#include "net/server.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::net;

namespace {

CellId coord(const VariantSpec& v, int file, int rank) {
  Coord c(2);
  c.c[0] = static_cast<std::int16_t>(file);
  c.c[1] = static_cast<std::int16_t>(rank);
  return v.dims.toCell(c);
}

Message helloFor(const VariantSpec& v) {
  Message hello;
  hello.kind = MessageKind::Hello;
  hello.version = kProtocolVersion;
  hello.variantHash = v.variantId();
  return hello;
}

Message intent(CellId from, CellId to) {
  Message m;
  m.kind = MessageKind::Intent;
  m.from = from;
  m.to = to;
  return m;
}

}  // namespace

TEST_CASE("an intent before the hello is refused and the game does not move",
          "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback l = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());

  // No hello at all: an intent on a fresh link is refused outright.
  l.a->send(encodeFrame(intent(coord(v, 4, 1), coord(v, 4, 3))));
  REQUIRE((*server)->poll());
  CHECK((*server)->game().plyCount() == 0);

  const auto frame = l.a->receive();
  REQUIRE(frame.has_value());
  const auto message = decodeFrame(*frame);
  REQUIRE(message.has_value());
  CHECK(message->kind == MessageKind::Refusal);
}

TEST_CASE("garbage on the wire is refused and the server stays up", "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback l = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());
  auto client = Client::create(*l.a, v.variantId());
  REQUIRE(client.has_value());
  REQUIRE((*server)->poll());  // the hello, answered with the opening state
  REQUIRE((*client)->poll());
  REQUIRE((*client)->synced());

  SECTION("a frame that is not TOML at all") {
    l.a->send("not a frame");
    REQUIRE((*server)->poll());
    (void)(*client)->poll();
    CHECK((*client)->lastRefusal() == "malformed frame");
  }
  SECTION("a length prefix that lies") {
    std::string lying(4, '\0');
    lying[0] = static_cast<char>(0xFF);
    l.a->send(lying);
    REQUIRE((*server)->poll());
    (void)(*client)->poll();
    CHECK_FALSE((*client)->lastRefusal().empty());
  }
  SECTION("a valid frame with a trailing byte") {
    l.a->send(encodeFrame(helloFor(v)) + "x");
    REQUIRE((*server)->poll());
    (void)(*client)->poll();
    CHECK_FALSE((*client)->lastRefusal().empty());
  }
  CHECK((*server)->game().plyCount() == 0);
}

TEST_CASE("an illegal or out-of-turn intent is refused", "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback l = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());
  auto client = Client::create(*l.a, v.variantId());
  REQUIRE(client.has_value());
  REQUIRE((*server)->poll());
  REQUIRE((*client)->poll());

  // White's own move: fine.
  (*client)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));
  REQUIRE((*server)->poll());
  REQUIRE((*client)->poll());
  CHECK((*server)->game().plyCount() == 1);

  // Replaying it is now illegal - e2 is empty.
  (*client)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));
  REQUIRE((*server)->poll());
  (void)(*client)->poll();
  CHECK((*server)->game().plyCount() == 1);
  CHECK_FALSE((*client)->lastRefusal().empty());

  // It is Black's turn; White's pieces cannot move. Find a White pawn and try.
  (*client)->sendIntent(coord(v, 0, 1), coord(v, 0, 2));
  REQUIRE((*server)->poll());
  (void)(*client)->poll();
  CHECK((*server)->game().plyCount() == 1);
}

TEST_CASE("random bytes never crash the server or move the game", "[unit][net]") {
  Loopback l = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());

  // A deterministic stream; the point is not coverage but that no input reaches past the
  // decoder into the game.
  std::uint32_t seed = 0x1234'5678;
  for (int i = 0; i < 500; ++i) {
    std::string bytes;
    const int n = static_cast<int>(seed % 40u);
    for (int k = 0; k < n; ++k) {
      seed = seed * 1664525u + 1013904223u;
      bytes.push_back(static_cast<char>((seed >> 16) & 0xFF));
    }
    l.a->send(bytes);
    (void)(*server)->poll();
    seed = seed * 1664525u + 1013904223u;
  }
  CHECK((*server)->game().plyCount() == 0);
}
