// SPDX-License-Identifier: GPL-3.0-or-later
#include "net/loopback.hpp"

#include <deque>
#include <utility>

namespace cb::net {
namespace {

class QueueTransport final : public Transport {
 public:
  QueueTransport(std::shared_ptr<std::deque<std::string>> out,
                 std::shared_ptr<std::deque<std::string>> in)
      : out_(std::move(out)), in_(std::move(in)) {}

  void send(std::string_view frame) override { out_->emplace_back(frame); }

  std::optional<std::string> receive() override {
    if (in_->empty()) return std::nullopt;
    std::string frame = std::move(in_->front());
    in_->pop_front();
    return frame;
  }

 private:
  std::shared_ptr<std::deque<std::string>> out_;
  std::shared_ptr<std::deque<std::string>> in_;
};

}  // namespace

Loopback makeLoopback() {
  // One queue per direction, shared by the two ends.
  auto a_to_b = std::make_shared<std::deque<std::string>>();
  auto b_to_a = std::make_shared<std::deque<std::string>>();
  Loopback l;
  l.a = std::make_unique<QueueTransport>(a_to_b, b_to_a);
  l.b = std::make_unique<QueueTransport>(b_to_a, a_to_b);
  return l;
}

}  // namespace cb::net
