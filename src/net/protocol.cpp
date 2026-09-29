// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/protocol.hpp"

#include <cstring>
#include <vector>

namespace cb::net {
namespace {

void putU16(std::string& out, std::uint16_t v) {
  out.push_back(static_cast<char>(v & 0xFF));
  out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void putU32(std::string& out, std::uint32_t v) {
  for (std::size_t i = 0; i < 4; ++i)
    out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void putU64(std::string& out, std::uint64_t v) {
  for (std::size_t i = 0; i < 8; ++i)
    out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}
void putString(std::string& out, std::string_view s) {
  putU32(out, static_cast<std::uint32_t>(s.size()));
  out.append(s);
}

class Reader {
 public:
  explicit Reader(std::string_view bytes) : bytes_(bytes) {}

  bool u8(std::uint8_t& out) {
    if (pos_ + 1 > bytes_.size()) return false;
    out = static_cast<std::uint8_t>(bytes_[pos_++]);
    return true;
  }
  bool u16(std::uint16_t& out) {
    if (pos_ + 2 > bytes_.size()) return false;
    out = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes_[pos_]) |
                                     (static_cast<unsigned char>(bytes_[pos_ + 1]) << 8));
    pos_ += 2;
    return true;
  }
  bool u32(std::uint32_t& out) {
    if (pos_ + 4 > bytes_.size()) return false;
    out = 0;
    for (std::size_t i = 0; i < 4; ++i) {
      out |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes_[pos_ + i]))
             << (8 * i);
    }
    pos_ += 4;
    return true;
  }
  bool u64(std::uint64_t& out) {
    if (pos_ + 8 > bytes_.size()) return false;
    out = 0;
    for (std::size_t i = 0; i < 8; ++i) {
      out |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes_[pos_ + i]))
             << (8 * i);
    }
    pos_ += 8;
    return true;
  }
  bool str(std::string& out) {
    std::uint32_t n = 0;
    if (!u32(n)) return false;
    if (pos_ + n > bytes_.size()) return false;
    out.assign(bytes_.substr(pos_, n));
    pos_ += n;
    return true;
  }
  [[nodiscard]] bool done() const { return pos_ == bytes_.size(); }

 private:
  std::string_view bytes_;
  std::size_t pos_{0};
};

}  // namespace

std::string encodeFrame(const Message& m) {
  std::string body;
  body.push_back(static_cast<char>(m.kind));
  switch (m.kind) {
    case MessageKind::Hello:
      putU16(body, m.version);
      putU64(body, m.variantHash);
      break;
    case MessageKind::Intent:
      putU32(body, m.from);
      putU32(body, m.to);
      break;
    case MessageKind::State:
      putU32(body, m.ply);
      body.push_back(static_cast<char>(m.sideToMove));
      putU64(body, m.positionHash);
      break;
    case MessageKind::Refusal:
      putString(body, m.reason);
      break;
  }
  std::string frame;
  putU32(frame, static_cast<std::uint32_t>(body.size()));
  frame += body;
  return frame;
}

Result<Message> decodeFrame(std::string_view frame) {
  Reader head(frame);
  std::uint32_t length = 0;
  if (!head.u32(length)) {
    return fail(ErrorCode::ParseError, "frame is shorter than its length prefix");
  }
  const std::string_view body = frame.substr(4);
  if (body.size() < length) {
    return fail(ErrorCode::ParseError, "frame is shorter than it says it is");
  }
  if (body.size() > length) {
    return fail(ErrorCode::ParseError, "frame has trailing bytes");
  }

  Reader r(body);
  std::uint8_t tag = 0;
  if (!r.u8(tag)) return fail(ErrorCode::ParseError, "empty frame body");

  Message m;
  switch (static_cast<MessageKind>(tag)) {
    case MessageKind::Hello:
      m.kind = MessageKind::Hello;
      if (!r.u16(m.version) || !r.u64(m.variantHash)) {
        return fail(ErrorCode::ParseError, "truncated Hello");
      }
      break;
    case MessageKind::Intent:
      m.kind = MessageKind::Intent;
      if (!r.u32(m.from) || !r.u32(m.to)) {
        return fail(ErrorCode::ParseError, "truncated Intent");
      }
      break;
    case MessageKind::State:
      m.kind = MessageKind::State;
      if (!r.u32(m.ply) || !r.u8(m.sideToMove) || !r.u64(m.positionHash)) {
        return fail(ErrorCode::ParseError, "truncated State");
      }
      break;
    case MessageKind::Refusal:
      m.kind = MessageKind::Refusal;
      if (!r.str(m.reason)) return fail(ErrorCode::ParseError, "truncated Refusal");
      break;
    default:
      return fail(ErrorCode::ParseError, "unknown message kind " + std::to_string(tag));
  }
  if (!r.done()) return fail(ErrorCode::ParseError, "message has trailing bytes");
  return m;
}

bool acceptsHello(const Message& hello, std::uint64_t localVariantHash) {
  return hello.kind == MessageKind::Hello && hello.version == kProtocolVersion &&
         hello.variantHash == localVariantHash;
}

}  // namespace cb::net
