// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "base/result.hpp"

namespace cb::net {

/// The protocol's version. A peer that speaks a different one is refused with a reason,
/// never guessed at - forward and backward compatibility has to be a decision, not an
/// accident of the byte layout.
inline constexpr std::uint16_t kProtocolVersion = 1;

enum class MessageKind : std::uint8_t {
  Hello,    ///< client -> server: my protocol version and the variant hash I expect
  Intent,   ///< client -> server: I want to play this move
  State,    ///< server -> client: the accepted position, by ply, side and hash
  Refusal,  ///< server -> client: why an intent was not played
};

/// One protocol message. A tagged union, small enough to pass by value: the wire is a
/// length-prefixed frame around the fields of whichever kind it is.
struct Message {
  MessageKind kind{MessageKind::Hello};

  std::uint16_t version{kProtocolVersion};  // Hello
  std::uint64_t variantHash{0};             // Hello: the VariantId the client expects

  std::uint32_t from{0};  // Intent: CellId
  std::uint32_t to{0};    // Intent: CellId

  std::uint32_t ply{0};           // State
  std::uint8_t sideToMove{0};     // State: 0 white, 1 black
  std::uint64_t positionHash{0};  // State

  std::string reason;  // Refusal
};

/// Frame a message: a 4-byte length, then the message's own bytes.
[[nodiscard]] std::string encodeFrame(const Message& message);

/// Decode one frame. Fails on a truncated frame, an unknown kind, or trailing bytes -
/// a decoder that tolerates junk is a decoder that hides a desync.
[[nodiscard]] Result<Message> decodeFrame(std::string_view frame);

/// Whether a peer's Hello is one we can play: same version, and it can name the variant.
/// A different variant is a clean refusal, never a game that diverges on move one.
[[nodiscard]] bool acceptsHello(const Message& hello, std::uint64_t localVariantHash);

}  // namespace cb::net
