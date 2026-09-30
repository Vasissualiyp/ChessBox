// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/server.hpp"

#include "net/protocol.hpp"

namespace cb::net {

Result<std::unique_ptr<Server>> Server::create(VariantSpec variant,
                                               Transport& transport) {
  if (!variant.finalized()) {
    return fail(ErrorCode::Internal, "a server needs a finalized variant");
  }
  // The private constructor is reachable here; make_unique is not.
  return std::unique_ptr<Server>(new Server(std::move(variant), transport));
}

void Server::sendState() {
  Message state;
  state.kind = MessageKind::State;
  state.ply = static_cast<std::uint32_t>(game_.plyCount());
  state.sideToMove = static_cast<std::uint8_t>(game_.position().sideToMove());
  state.positionHash = game_.position().hash();
  transport_->send(encodeFrame(state));
}

void Server::sendRefusal(const std::string& reason) {
  Message refusal;
  refusal.kind = MessageKind::Refusal;
  refusal.reason = reason;
  transport_->send(encodeFrame(refusal));
}

bool Server::poll() {
  while (auto frame = transport_->receive()) {
    const auto message = decodeFrame(*frame);
    if (!message.has_value()) {
      sendRefusal("malformed frame");
      continue;
    }
    switch (message->kind) {
      case MessageKind::Hello: {
        if (!acceptsHello(*message, variant_.variantId())) {
          sendRefusal("variant or protocol mismatch");
          return false;
        }
        helloed_ = true;
        sendState();
        break;
      }
      case MessageKind::Intent: {
        if (!helloed_) {
          sendRefusal("say hello first");
          break;
        }
        const Move* chosen = nullptr;
        for (const Move& m : game_.legalMoves()) {
          if (m.from == message->from && m.to == message->to) {
            chosen = &m;
            break;
          }
        }
        if (chosen == nullptr) {
          sendRefusal("not a legal move");
          break;
        }
        if (auto ok = game_.play(*chosen); !ok.has_value()) {
          sendRefusal("the engine rejected the move");
          break;
        }
        sendState();
        break;
      }
      default:
        sendRefusal("unexpected message");
        break;
    }
  }
  return true;
}

}  // namespace cb::net
