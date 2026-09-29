// SPDX-License-Identifier: GPL-3.0-or-later
#include "temporal/timeline.hpp"

#include <algorithm>

namespace cb::temporal {

bool Timeline::hasBoard(Turn t) const {
  return std::find(boards.begin(), boards.end(), t) != boards.end();
}

TimelineModel::TimelineModel(TemporalPolicy policy) : policy_(policy) {
  Timeline original;
  original.id = policy_.originLine;
  original.owner = BranchOwner::Original;
  original.order = 0;
  lines_.push_back(original);
}

int TimelineModel::signOf(BranchOwner owner) const noexcept {
  return owner == BranchOwner::White ? policy_.whiteSign : policy_.blackSign;
}

LineId TimelineModel::nextId(BranchOwner owner) const {
  const int order = (owner == BranchOwner::White ? whiteBranches_ : blackBranches_) + 1;
  return static_cast<LineId>(policy_.originLine + signOf(owner) * order);
}

const Timeline* TimelineModel::find(LineId line) const {
  for (const Timeline& t : lines_) {
    if (t.id == line) return &t;
  }
  return nullptr;
}

Timeline* TimelineModel::findMutable(LineId line) {
  for (Timeline& t : lines_) {
    if (t.id == line) return &t;
  }
  return nullptr;
}

bool TimelineModel::addBoard(LineId line, Turn turn) {
  Timeline* t = findMutable(line);
  if (t == nullptr || t->hasBoard(turn)) return false;
  t->boards.push_back(turn);
  std::sort(t->boards.begin(), t->boards.end());
  return true;
}

std::optional<LineId> TimelineModel::branch(BranchOwner owner, LineId parentLine,
                                            Turn parentTurn) {
  if (owner == BranchOwner::Original) return std::nullopt;
  Timeline t;
  t.id = nextId(owner);
  t.owner = owner;
  t.parentLine = parentLine;
  t.parentTurn = parentTurn;
  if (owner == BranchOwner::White) {
    t.order = ++whiteBranches_;
  } else {
    t.order = ++blackBranches_;
  }
  lines_.push_back(t);
  return t.id;
}

bool TimelineModel::active(LineId line) const {
  const Timeline* t = find(line);
  if (t == nullptr) return false;
  if (t->owner == BranchOwner::Original) return true;
  // The nth timeline a player created is active if the opponent created at least n-1.
  const int opponentBranches =
      t->owner == BranchOwner::White ? blackBranches_ : whiteBranches_;
  return opponentBranches >= t->order - 1;
}

Turn TimelineModel::presentTurn() const {
  Turn present = -1;
  for (const Timeline& t : lines_) {
    if (!active(t.id) || t.boards.empty()) continue;
    const Turn latest = t.latest();
    if (present < 0 || latest < present) present = latest;
  }
  return present;
}

std::vector<BoardRef> TimelineModel::boardsInPresent() const {
  std::vector<BoardRef> out;
  const Turn present = presentTurn();
  if (present < 0) return out;
  for (const Timeline& t : lines_) {
    if (!active(t.id)) continue;
    if (t.hasBoard(present)) out.push_back(BoardRef{present, t.id});
  }
  return out;
}

}  // namespace cb::temporal
