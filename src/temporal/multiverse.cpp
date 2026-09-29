// SPDX-License-Identifier: GPL-3.0-or-later
#include "temporal/multiverse.hpp"

namespace cb::temporal {

Multiverse::Multiverse(TemporalPolicy policy, Turn maxTurn, LineId maxLineCoord)
    : maxTurn_(maxTurn), maxLineCoord_(maxLineCoord), policy_(policy), lines_(policy) {
  add(BoardKey{0, policy_.originLine}, Color::White);
}

void Multiverse::add(const BoardKey& b, Color side) {
  if (live_.count(b) != 0) return;
  lines_.addBoard(b.line, b.turn);
  live_.insert(b);
  side_[b] = side;
}

bool Multiverse::playable(const BoardKey& b) const {
  const Timeline* t = lines_.find(b.line);
  return t != nullptr && exists(b) && t->latest() == b.turn;
}

Color Multiverse::sideToMove(const BoardKey& b) const {
  const auto it = side_.find(b);
  return it == side_.end() ? Color::White : it->second;
}

std::vector<BoardKey> Multiverse::present() const {
  std::vector<BoardKey> out;
  const Turn t = lines_.presentTurn();
  if (t < 0) return out;
  for (const BoardKey& b : live_) {
    if (b.turn == t && lines_.active(b.line)) out.push_back(b);
  }
  return out;
}

Color Multiverse::mover() const {
  const std::vector<BoardKey> p = present();
  return p.empty() ? Color::White : sideToMove(p.front());
}

std::vector<BoardKey> Multiverse::unansweredPresent() const {
  std::vector<BoardKey> out;
  for (const BoardKey& b : present()) {
    if (answered_.count(b) == 0) out.push_back(b);
  }
  return out;
}

std::optional<std::vector<BoardKey>> Multiverse::apply(const BoardKey& source,
                                                       const BoardKey& destination) {
  if (!playable(source)) return std::nullopt;
  const Color moverSide = sideToMove(source);
  const Color newcomer = opponent(moverSide);

  // Plan first, mutate second: a move that cannot fit must leave the multiverse alone.
  const BoardKey sf = futureOf(source);
  if (sf.turn > maxTurn_ || exists(sf)) return std::nullopt;

  // A move that stays on the source board, or steps one turn forward into its own
  // future, produces a single new board carrying the piece. Treating the second case as
  // travel used to branch a timeline onto an empty board, which put the piece in the
  // wrong place entirely.
  if (source == destination || destination == sf) {
    add(sf, newcomer);
    answered_.insert(source);
    return std::vector<BoardKey>{sf};
  }

  // Otherwise the piece travels to a board that must already exist.
  if (!exists(destination)) return std::nullopt;

  std::vector<BoardKey> created{sf};
  std::optional<LineId> branchLine;
  std::optional<BranchOwner> branchOwner;
  const BoardKey destinationFuture{
      static_cast<Turn>(destination.turn + policy_.branchAdvance), destination.line};
  if (destinationFuture.turn > maxTurn_) return std::nullopt;
  if (playable(destination) && !exists(destinationFuture)) {
    // The destination is the latest board on its line, so it simply advances too.
    created.push_back(destinationFuture);
  } else {
    // Into the past: a fresh timeline whose board is the target one branch-advance on.
    const BranchOwner owner =
        moverSide == Color::White ? BranchOwner::White : BranchOwner::Black;
    const LineId candidate = lines_.nextId(owner);
    if (candidate < 0 || candidate > maxLineCoord_) return std::nullopt;
    branchLine = candidate;
    branchOwner = owner;
    created.push_back(BoardKey{destinationFuture.turn, candidate});
  }

  // Commit.
  add(sf, newcomer);
  if (!branchLine.has_value()) {
    add(destinationFuture, opponent(sideToMove(destination)));
  } else {
    (void)lines_.branch(*branchOwner, destination.line, destination.turn);
    add(BoardKey{destinationFuture.turn, *branchLine}, newcomer);
  }
  answered_.insert(source);
  return created;
}

}  // namespace cb::temporal
