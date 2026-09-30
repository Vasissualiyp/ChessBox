// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/server.hpp"

#include "net/protocol.hpp"

namespace cb::net {

Result<std::unique_ptr<Server>> Server::create(VariantSpec variant) {
  if (!variant.finalized()) {
    return fail(ErrorCode::Internal, "a server needs a finalized variant");
  }
  return std::unique_ptr<Server>(new Server(std::move(variant)));
}

Result<std::unique_ptr<Server>> Server::create(VariantSpec variant,
                                               Transport& transport) {
  auto server = create(std::move(variant));
  if (!server.has_value()) return server;
  (*server)->addClient(transport);
  return server;
}

void Server::addClient(Transport& transport) {
  links_.push_back(Link{&transport, false});
}

void Server::sendState(Transport& to) const {
  Message state;
  state.kind = MessageKind::State;
  state.ply = static_cast<std::uint32_t>(game_.plyCount());
  state.sideToMove = static_cast<std::uint8_t>(game_.position().sideToMove());
  state.positionHash = game_.position().hash();
  to.send(encodeFrame(state));
}

void Server::broadcastState() const {
  for (const Link& link : links_) {
    if (link.helloed) sendState(*link.transport);
  }
}

void Server::sendRefusal(Transport& to, const std::string& reason) {
  Message refusal;
  refusal.kind = MessageKind::Refusal;
  refusal.reason = reason;
  to.send(encodeFrame(refusal));
}

const Move* Server::findLegal(std::uint32_t from, std::uint32_t to) const {
  for (const Move& m : game_.legalMoves()) {
    if (m.from == from && m.to == to) return &m;
  }
  return nullptr;
}

bool Server::poll() {
  for (auto it = links_.begin(); it != links_.end();) {
    bool drop = false;
    while (auto frame = it->transport->receive()) {
      const auto message = decodeFrame(*frame);
      if (!message.has_value()) {
        sendRefusal(*it->transport, "malformed frame");
        continue;
      }
      if (message->kind == MessageKind::Hello) {
        if (!acceptsHello(*message, variant_.variantId())) {
          if (!source_.empty()) {
            // The client does not have this variant. Send the file so it can load it and
            // ask again; the link stays open but is not a player of this game until then.
            Message variant;
            variant.kind = MessageKind::VariantSource;
            variant.source = source_;
            it->transport->send(encodeFrame(variant));
            it->helloed = false;
            continue;
          }
          sendRefusal(*it->transport, "variant or protocol mismatch");
          drop = true;  // this link is not for this game
          break;
        }
        it->helloed = true;
        sendState(*it->transport);  // sync - the same for a first join or a reconnect
        continue;
      }
      if (!it->helloed) {
        sendRefusal(*it->transport, "say hello first");
        continue;
      }
      if (message->kind == MessageKind::Intent) {
        const Move* chosen = findLegal(message->from, message->to);
        if (chosen == nullptr) {
          sendRefusal(*it->transport, "not a legal move");
          continue;
        }
        if (auto ok = game_.play(*chosen); !ok.has_value()) {
          sendRefusal(*it->transport, "the engine rejected the move");
          continue;
        }
        broadcastState();
        continue;
      }
      sendRefusal(*it->transport, "unexpected message");
    }
    it = drop ? links_.erase(it) : it + 1;
  }
  return !links_.empty();
}

}  // namespace cb::net
