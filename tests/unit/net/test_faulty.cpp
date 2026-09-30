// SPDX-License-Identifier: GPL-3.0-or-later
//
// M8.4: the network harness. A faulty link corrupts frames deterministically; the server
// must refuse every bad one and still play a good one, always leaving the game in a state
// both sides agree on.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "net/client.hpp"
#include "net/faulty.hpp"
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

TEST_CASE("a truncated frame is refused and a later good move still plays",
          "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback l = makeLoopback();
  // Every second frame the client sends loses its last byte. The hello is frame one, so
  // the first intent is mangled and the second is clean.
  FaultyTransport faulty(*l.a, Faults{0, /*truncateEvery=*/2, 0});
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());
  auto client = Client::create(faulty, v.variantId());
  REQUIRE(client.has_value());

  REQUIRE((*server)->poll());  // hello (intact)
  REQUIRE((*client)->poll());
  REQUIRE((*client)->synced());

  (*client)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));  // frame 2: mangled
  REQUIRE((*server)->poll());
  CHECK((*server)->game().plyCount() == 0);  // never reached the game
  (void)(*client)->poll();
  CHECK_FALSE((*client)->lastRefusal().empty());

  (*client)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));  // frame 3: clean
  REQUIRE((*server)->poll());
  REQUIRE((*client)->poll());
  CHECK((*server)->game().plyCount() == 1);
  CHECK((*client)->positionHash() == (*server)->positionHash());
}

TEST_CASE("a duplicated frame is not a second move", "[unit][net]") {
  const VariantSpec v = test::loadVariant("standard");
  Loopback l = makeLoopback();
  // Every intent is delivered twice.
  FaultyTransport faulty(*l.a, Faults{0, 0, /*duplicateEvery=*/2});
  auto server = Server::create(test::loadVariant("standard"), *l.b);
  REQUIRE(server.has_value());
  auto client = Client::create(faulty, v.variantId());
  REQUIRE(client.has_value());
  REQUIRE((*server)->poll());
  REQUIRE((*client)->poll());

  (*client)->sendIntent(coord(v, 4, 1), coord(v, 4, 3));
  REQUIRE((*server)->poll());
  // The second copy is the same move from an empty square: refused, so still one ply.
  CHECK((*server)->game().plyCount() == 1);
}
