// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "net/transport.hpp"

namespace cb::net {

/// Faults to inject on the way out, applied deterministically by a frame counter: the
/// same run drops and corrupts the same frames every time, so a test is reproducible.
struct Faults {
  int dropEvery{0};       ///< drop every Nth frame sent
  int truncateEvery{0};   ///< cut the last byte off every Nth frame
  int duplicateEvery{0};  ///< send every Nth frame twice
};

/// A transport that corrupts what passes through it. The point is not to simulate TCP -
/// which does none of this - but to prove the far side's decoder and server stay correct
/// whatever arrives: a garbled frame is a clean refusal, never a move.
class FaultyTransport final : public Transport {
 public:
  FaultyTransport(Transport& inner, Faults faults) : inner_(inner), faults_(faults) {}

  void send(std::string_view frame) override {
    ++sent_;
    if (faults_.dropEvery > 0 &&
        sent_ % static_cast<std::uint64_t>(faults_.dropEvery) == 0)
      return;
    std::string out(frame);
    if (faults_.truncateEvery > 0 &&
        sent_ % static_cast<std::uint64_t>(faults_.truncateEvery) == 0 && !out.empty()) {
      out.pop_back();
    }
    inner_.send(out);
    if (faults_.duplicateEvery > 0 &&
        sent_ % static_cast<std::uint64_t>(faults_.duplicateEvery) == 0) {
      inner_.send(out);
    }
  }

  std::optional<std::string> receive() override { return inner_.receive(); }

 private:
  Transport& inner_;
  Faults faults_;
  std::uint64_t sent_{0};
};

}  // namespace cb::net
