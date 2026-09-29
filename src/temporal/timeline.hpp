// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace cb::temporal {

/// A timeline index along the multiverse axis. The original timeline is 0; White's
/// branches run one way and Black's the other, so a line id's sign says who owns it.
using LineId = std::int16_t;

/// A half-turn index along the time axis. Board 0 is the starting position; every move
/// creates a board one half-turn to its right.
using Turn = std::int16_t;

/// Who a timeline belongs to. The original timeline belongs to nobody in particular.
enum class BranchOwner : std::uint8_t { Original, White, Black };

/// One board in the multiverse: a full position at a point in time on a timeline.
struct BoardRef {
  Turn turn{0};
  LineId line{0};
};

/// One timeline: the boards that exist on it, who created it, and when.
struct Timeline {
  LineId id{0};
  BranchOwner owner{BranchOwner::Original};
  /// 1-based rank among its owner's branches; 0 for the original timeline. This is the
  /// number the activity rule counts.
  int order{0};
  std::vector<Turn> boards;  // ascending; a board exists at each

  [[nodiscard]] Turn latest() const noexcept {
    return boards.empty() ? static_cast<Turn>(-1) : boards.back();
  }
  [[nodiscard]] bool hasBoard(Turn t) const;
};

/// The multiverse's history: which boards exist, on which timeline, and who created each
/// timeline. It is enough to apply the published activity and present rules, and it is
/// deliberately just data - no positions, no moves.
///
/// The rules implemented here are transcribed from the reference game's public rules:
///
///  - A timeline is *playable* if the board on it is the latest on that timeline.
///  - The original timeline is *active*. The nth timeline a player created is active iff
///    the opponent has created at least n-1 timelines.
///  - The *present line* aligns with the active board furthest left along the time axis;
///    every board in that column (same turn) is in the present.
///
/// Nothing here is validated against the reference game's own outputs yet; treat it as a
/// first-principles implementation of the written rules (M6.2).
class TimelineModel {
 public:
  TimelineModel();

  [[nodiscard]] const std::vector<Timeline>& timelines() const noexcept { return lines_; }
  [[nodiscard]] const Timeline* find(LineId line) const;

  /// Record that a board exists at (turn, line). A duplicate is refused.
  bool addBoard(LineId line, Turn turn);

  /// Create a timeline for `owner`, branching from `fromLine` into the vacant row closest
  /// to it on that player's side: White branches to lower ids, Black to higher ones.
  /// Returns the new line, or nullopt if `owner` is the original.
  std::optional<LineId> branch(BranchOwner owner, LineId fromLine);

  /// The nth timeline a player created is active iff the opponent created at least n-1.
  [[nodiscard]] bool active(LineId line) const;

  /// The active board furthest left along the time axis sets the present turn.
  [[nodiscard]] Turn presentTurn() const;

  /// Every board in the present column, across the active timelines: the boards a player
  /// has to answer on.
  [[nodiscard]] std::vector<BoardRef> boardsInPresent() const;

 private:
  [[nodiscard]] Timeline* findMutable(LineId line);

  std::vector<Timeline> lines_;
  int whiteBranches_{0};
  int blackBranches_{0};
};

}  // namespace cb::temporal
