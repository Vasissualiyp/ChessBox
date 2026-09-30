// SPDX-License-Identifier: GPL-3.0-or-later
//
// M8.1/M8.2: the authoritative server and the thin client, wired through the in-process
// transport. The server is the only thing that advances the game, and both sides end a
// turn agreeing on the position hash - which is what makes a desync a loud failure.
#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "net/client.hpp"
#include "net/loopback.hpp"
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

}  // namespace

TEST_CASE("the server validates an intent and both sides agree on the hash",
          "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback l = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());
  auto client = Client::create(*l.a, v.variantId());
  REQUIRE(client.has_value());

  REQUIRE((*server)->poll());  // the hello is answered with the opening state
  REQUIRE((*client)->poll());
  CHECK((*client)->synced());
  CHECK((*client)->ply() == 0);
  CHECK((*client)->positionHash() == (*server)->positionHash());

  (*client)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));  // e2-e4
  REQUIRE((*server)->poll());
  REQUIRE((*client)->poll());
  CHECK((*client)->ply() == 1);
  CHECK((*client)->positionHash() == (*server)->positionHash());
  CHECK((*server)->game().plyCount() == 1);

  // The same intent again is illegal; the server refuses it and the game is unchanged.
  (*client)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));
  REQUIRE((*server)->poll());
  CHECK_FALSE((*client)->poll());  // a Refusal came back
  CHECK_FALSE((*client)->lastRefusal().empty());
  CHECK((*server)->game().plyCount() == 1);
}

TEST_CASE("the server turns away a client that named a different variant",
          "[unit][net]") {
  Loopback l = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());
  auto client = Client::create(*l.a, /*variantHash=*/0xDEAD'BEEF);
  REQUIRE(client.has_value());
  CHECK_FALSE((*server)->poll());  // the bad hello closes the link
}

TEST_CASE("a move is broadcast to every client", "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback a = makeLoopback();
  Loopback b = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"));
  REQUIRE(server.has_value());
  (*server)->addClient(*a.b);
  (*server)->addClient(*b.b);

  auto ca = Client::create(*a.a, v.variantId());
  auto cb = Client::create(*b.a, v.variantId());
  REQUIRE(ca.has_value());
  REQUIRE(cb.has_value());

  REQUIRE((*server)->poll());
  REQUIRE((*ca)->poll());
  REQUIRE((*cb)->poll());
  CHECK((*ca)->synced());
  CHECK((*cb)->synced());
  CHECK((*server)->clientCount() == 2);

  (*ca)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));  // e2-e4
  REQUIRE((*server)->poll());
  REQUIRE((*ca)->poll());
  REQUIRE((*cb)->poll());
  CHECK((*ca)->ply() == 1);
  CHECK((*cb)->ply() == 1);  // the other client saw it too
  CHECK((*ca)->positionHash() == (*cb)->positionHash());
  CHECK((*ca)->positionHash() == (*server)->positionHash());
}

TEST_CASE("a client joining mid-game is resynced to the current position",
          "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback a = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"), *a.b);
  REQUIRE(server.has_value());
  auto ca = Client::create(*a.a, v.variantId());
  REQUIRE(ca.has_value());

  REQUIRE((*server)->poll());
  REQUIRE((*ca)->poll());
  (*ca)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));
  REQUIRE((*server)->poll());
  REQUIRE((*ca)->poll());
  REQUIRE((*ca)->ply() == 1);

  // A late client's hello is answered with where the game *is*, not the opening.
  Loopback late = makeLoopback();
  (*server)->addClient(*late.b);
  auto cl = Client::create(*late.a, v.variantId());
  REQUIRE(cl.has_value());
  REQUIRE((*server)->poll());
  REQUIRE((*cl)->poll());
  CHECK((*cl)->synced());
  CHECK((*cl)->ply() == 1);
  CHECK((*cl)->positionHash() == (*server)->positionHash());
}

TEST_CASE("a client that lacks the variant is sent its source", "[unit][net]") {
  Loopback l = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());
  (*server)->setVariantSource("name = \"from-the-server\"\n");
  auto client = Client::create(*l.a, /*a variant it does not have*/ 0xBEEF);
  REQUIRE(client.has_value());

  REQUIRE((*server)->poll());
  REQUIRE((*client)->poll());
  CHECK((*client)->variantSource() == "name = \"from-the-server\"\n");
  CHECK_FALSE((*client)->synced());  // not a player of this game yet
}

TEST_CASE("a spectator receives the moves without ever sending any", "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback player = makeLoopback();
  Loopback watcher = makeLoopback();
  auto server = Server::create(test::loadVariant("standard"));
  REQUIRE(server.has_value());
  (*server)->addClient(*player.b);
  (*server)->addClient(*watcher.b);

  auto p = Client::create(*player.a, v.variantId());
  auto w = Client::create(*watcher.a, v.variantId());
  REQUIRE(p.has_value());
  REQUIRE(w.has_value());
  REQUIRE((*server)->poll());
  REQUIRE((*p)->poll());
  REQUIRE((*w)->poll());

  (*p)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));  // e2-e4
  REQUIRE((*server)->poll());
  REQUIRE((*p)->poll());
  REQUIRE((*w)->poll());
  CHECK((*w)->ply() == 1);  // saw it, sent nothing
  CHECK((*w)->positionHash() == (*server)->positionHash());
}
