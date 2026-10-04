// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/game_runner.hpp"

#include <algorithm>

#include "app/session.hpp"

namespace cb::app {

GameRunner::GameRunner(const GameFile& file, Session& session)
    : file_(file), session_(session) {}

Result<void> GameRunner::reset() {
  if (auto ok = verifyGameFileVariant(file_, session_.variant()); !ok.has_value()) {
    return ok;
  }
  if (auto ok = session_.loadFen(file_.startFen); !ok.has_value()) return ok;
  index_ = 0;
  lastTravel_ = 0.0f;
  return {};
}

Result<void> GameRunner::step() {
  if (done()) {
    return fail(ErrorCode::OutOfRange, "the game has no move left to play");
  }
  const std::string& text = file_.moves[index_];
  if (auto ok = session_.playMoveText(text); !ok.has_value()) {
    return fail(ok.error().code, gameFileIllegalMove(index_ + 1, text));
  }
  lastTravel_ = session_.animation().duration();
  ++index_;
  return {};
}

PlaybackCursor playbackCursor(const std::vector<float>& bodySeconds, float dwellSeconds,
                              float seconds) {
  PlaybackCursor cursor;
  if (bodySeconds.empty()) return cursor;
  const float dwell = std::max(0.0f, dwellSeconds);
  float t = std::max(0.0f, seconds);
  for (std::size_t i = 0; i < bodySeconds.size(); ++i) {
    const float body = std::max(0.0f, bodySeconds[i]);
    if (t < body) {
      cursor.move = i;
      cursor.elapsed = t;
      return cursor;
    }
    t -= body;
    if (i + 1 < bodySeconds.size()) {
      if (t < dwell) {
        // Held after the move landed: the body's own end is the pose to draw.
        cursor.move = i;
        cursor.elapsed = body;
        return cursor;
      }
      t -= dwell;
    }
  }
  // Past the end: hold the final landed pose.
  cursor.move = bodySeconds.size() - 1;
  cursor.elapsed = std::max(0.0f, bodySeconds.back());
  return cursor;
}

float playbackTotalSeconds(const std::vector<float>& bodySeconds, float dwellSeconds) {
  if (bodySeconds.empty()) return 0.0f;
  const float dwell = std::max(0.0f, dwellSeconds);
  float total = 0.0f;
  for (float b : bodySeconds) total += std::max(0.0f, b);
  total += dwell * static_cast<float>(bodySeconds.size() - 1);
  return total;
}

}  // namespace cb::app
