// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/client.hpp"

#include "net/protocol.hpp"

namespace cb::net {

Result<std::unique_ptr<Client>> Client::create(Transport& transport,
                                               std::uint64_t variantHash) {
  auto client = std::unique_ptr<Client>(new Client(transport));
  Message hello;
  hello.kind = MessageKind::Hello;
  hello.version = kProtocolVersion;
  hello.variantHash = variantHash;
  transport.send(encodeFrame(hello));
  return client;
}

void Client::sendIntent(CellId from, CellId to) {
  Message intent;
  intent.kind = MessageKind::Intent;
  intent.from = from;
  intent.to = to;
  transport_->send(encodeFrame(intent));
}

bool Client::poll() {
  while (auto frame = transport_->receive()) {
    const auto message = decodeFrame(*frame);
    if (!message.has_value()) continue;
    switch (message->kind) {
      case MessageKind::State:
        ply_ = message->ply;
        hash_ = message->positionHash;
        synced_ = true;
        break;
      case MessageKind::Refusal:
        refusal_ = message->reason;
        return false;
      default:
        break;
    }
  }
  return true;
}

}  // namespace cb::net
