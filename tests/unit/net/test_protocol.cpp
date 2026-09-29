// SPDX-License-Identifier: GPL-3.0-or-later
//
// M8.1/M8.2: the multiplayer protocol. The wire is tested without a wire - frames are
// just bytes, and every failure has to be a clean parse error, never a guess.
#include <catch2/catch_test_macros.hpp>

#include <string>

#include "net/protocol.hpp"

using namespace cb;
using namespace cb::net;

TEST_CASE("every message round-trips through its frame", "[unit][net]") {
  SECTION("Hello") {
    Message hello;
    hello.kind = MessageKind::Hello;
    hello.version = kProtocolVersion;
    hello.variantHash = 0x0123'4567'89AB'CDEFULL;
    const auto back = decodeFrame(encodeFrame(hello));
    REQUIRE(back.has_value());
    CHECK(back->kind == MessageKind::Hello);
    CHECK(back->version == kProtocolVersion);
    CHECK(back->variantHash == 0x0123'4567'89AB'CDEFULL);
  }
  SECTION("Intent") {
    Message intent;
    intent.kind = MessageKind::Intent;
    intent.from = 12;
    intent.to = 28;
    const auto back = decodeFrame(encodeFrame(intent));
    REQUIRE(back.has_value());
    CHECK(back->kind == MessageKind::Intent);
    CHECK(back->from == 12);
    CHECK(back->to == 28);
  }
  SECTION("State") {
    Message state;
    state.kind = MessageKind::State;
    state.ply = 7;
    state.sideToMove = 1;
    state.positionHash = 0xDEAD'BEEF'CAFE'0001ULL;
    const auto back = decodeFrame(encodeFrame(state));
    REQUIRE(back.has_value());
    CHECK(back->kind == MessageKind::State);
    CHECK(back->ply == 7);
    CHECK(back->sideToMove == 1);
    CHECK(back->positionHash == 0xDEAD'BEEF'CAFE'0001ULL);
  }
  SECTION("Refusal") {
    Message refusal;
    refusal.kind = MessageKind::Refusal;
    refusal.reason = "not your turn";
    const auto back = decodeFrame(encodeFrame(refusal));
    REQUIRE(back.has_value());
    CHECK(back->kind == MessageKind::Refusal);
    CHECK(back->reason == "not your turn");
  }
}

TEST_CASE("the decoder refuses junk rather than guessing", "[unit][net]") {
  Message intent;
  intent.kind = MessageKind::Intent;
  intent.from = 1;
  intent.to = 2;
  const std::string frame = encodeFrame(intent);

  SECTION("a truncated frame") {
    const auto back = decodeFrame(frame.substr(0, frame.size() - 1));
    CHECK_FALSE(back.has_value());
  }
  SECTION("a frame with trailing bytes") {
    CHECK_FALSE(decodeFrame(frame + "x").has_value());
  }
  SECTION("shorter than its length prefix") {
    const auto back = decodeFrame(std::string("\xFF\x00\x00\x00", 4));
    CHECK_FALSE(back.has_value());
  }
  SECTION("an unknown tag") {
    // length 1, then a kind no one defined.
    const std::string bogus = std::string("\x01\x00\x00\x00", 4) + std::string(1, '\x7F');
    CHECK_FALSE(decodeFrame(bogus).has_value());
  }
}

TEST_CASE("a peer is refused for a version or variant it cannot play", "[unit][net]") {
  Message hello;
  hello.kind = MessageKind::Hello;
  hello.version = kProtocolVersion;
  hello.variantHash = 0x11;

  CHECK(acceptsHello(hello, 0x11));
  CHECK_FALSE(acceptsHello(hello, 0x22));  // a different game

  hello.version = static_cast<std::uint16_t>(kProtocolVersion + 1);
  CHECK_FALSE(acceptsHello(hello, 0x11));  // a different protocol

  Message intent;
  intent.kind = MessageKind::Intent;
  CHECK_FALSE(acceptsHello(intent, 0x11));  // not a hello at all
}
