// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "base/result.hpp"
#include "io/game_file.hpp"

namespace cb {
struct VariantSpec;
}

namespace cb::app {

class Session;

/// The deterministic runner over a game file (M12.6). It steps a `GameFile`'s moves one
/// at a time through a `Session`, so each can be animated and its camera simulated -
/// unlike `applyGameFile`, which replays them all at once. It reads no clock and no
/// previous state: given the same file and session it makes the same calls, which is what
/// makes a clip of the whole game reproducible.
class GameRunner {
 public:
  GameRunner(const GameFile& file, Session& session);

  /// Validate the file against the session's variant and load its start position. Call
  /// before stepping. Fails, exactly as `applyGameFile` does, when the recorded
  /// `VariantId` no longer matches.
  Result<void> reset();

  /// Apply the next move, starting its animation. Fails naming the move and the position
  /// when it is not legal, leaving the last legal move in place.
  Result<void> step();

  [[nodiscard]] bool done() const noexcept { return index_ >= file_.moves.size(); }
  [[nodiscard]] std::size_t index() const noexcept { return index_; }
  [[nodiscard]] std::size_t count() const noexcept { return file_.moves.size(); }
  [[nodiscard]] const GameFile& file() const noexcept { return file_; }

  /// The move animation duration of the move just stepped, in seconds. Zero before the
  /// first step, or when no animation runs.
  [[nodiscard]] float lastTravelSeconds() const noexcept { return lastTravel_; }

 private:
  const GameFile& file_;
  Session& session_;
  std::size_t index_{0};
  float lastTravel_{0.0f};
};

/// A point in a played-back game: which move, and how far into its body. During a dwell
/// `elapsed` is held at the move's full body, so the picture is the landed pose.
struct PlaybackCursor {
  std::size_t move{0};
  float elapsed{0.0f};
};

/// Map a global time to (move, elapsed-within-move), given each move's body duration and
/// a dwell held after every move but the last. Pure, so a frame can be requested at any
/// time in any order and reproduced.
[[nodiscard]] PlaybackCursor playbackCursor(const std::vector<float>& bodySeconds,
                                            float dwellSeconds, float seconds);

/// The whole played-back game's length: the sum of the bodies plus a dwell after every
/// move but the last. Zero for a game with no moves.
[[nodiscard]] float playbackTotalSeconds(const std::vector<float>& bodySeconds,
                                         float dwellSeconds);

}  // namespace cb::app
