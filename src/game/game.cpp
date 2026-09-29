// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/game.hpp"

#include <algorithm>

namespace cb {

std::string_view toString(GameResult r) noexcept {
  switch (r) {
    case GameResult::InProgress:
      return "in progress";
    case GameResult::WhiteWins:
      return "White wins";
    case GameResult::BlackWins:
      return "Black wins";
    case GameResult::Draw:
      return "draw";
  }
  return "?";
}

std::string_view toString(EndReason r) noexcept {
  switch (r) {
    case EndReason::None:
      return "";
    case EndReason::Checkmate:
      return "checkmate";
    case EndReason::Stalemate:
      return "stalemate";
    case EndReason::DrawClock:
      return "the draw clock";
    case EndReason::Repetition:
      return "threefold repetition";
    case EndReason::NoLegalMoves:
      return "no legal moves";
  }
  return "?";
}

Game::Game(const VariantSpec& v) : v_(&v), pos_(Position::startPosition(v)), gen_(v) {
  hashes_.push_back(pos_.hash());
}

void Game::invalidate() {
  legalCacheValid_ = false;
}

const std::vector<Move>& Game::legalMoves() const {
  if (!legalCacheValid_) {
    MoveList list(v_->moveUpperBound());
    // generateLegal needs a mutable position for make/unmake, but leaves it exactly as
    // it found it - which the reversibility property test is what guarantees.
    gen_.generateLegal(const_cast<Position&>(pos_), list);
    legalCache_.assign(list.begin(), list.end());
    legalCacheValid_ = true;
  }
  return legalCache_;
}

bool Game::isLegal(const Move& m) const {
  const auto& moves = legalMoves();
  return std::find(moves.begin(), moves.end(), m) != moves.end();
}

Result<void> Game::play(const Move& m) {
  if (!isLegal(m)) {
    return fail(ErrorCode::ValidationError, "that move is not legal in this position");
  }
  Undo u;
  pos_.make(m, u);
  history_.push_back(u);
  played_.push_back(m);
  hashes_.push_back(pos_.hash());
  invalidate();
  return {};
}

bool Game::undo() {
  if (history_.empty()) return false;
  pos_.unmake(played_.back(), history_.back());
  history_.pop_back();
  played_.pop_back();
  hashes_.pop_back();
  invalidate();
  return true;
}

void Game::reset() {
  pos_ = Position::startPosition(*v_);
  history_.clear();
  played_.clear();
  hashes_.clear();
  hashes_.push_back(pos_.hash());
  invalidate();
}

bool Game::inCheck() const {
  return gen_.inCheck(pos_, pos_.sideToMove());
}

int Game::repetitionCount() const {
  const std::uint64_t current = pos_.hash();
  return static_cast<int>(std::count(hashes_.begin(), hashes_.end(), current));
}

EndReason Game::endReason() const {
  if (!legalMoves().empty()) {
    if (v_->halfmoveDrawLimit > 0 && pos_.halfmoveClock() >= v_->halfmoveDrawLimit) {
      return EndReason::DrawClock;
    }
    if (repetitionCount() >= 3) return EndReason::Repetition;
    return EndReason::None;
  }
  // No legal moves. What that means is the variant's business: a game with no royal
  // piece has no checkmate, and stalemate is a draw, a loss or a win by declaration.
  if (pos_.findRoyal(pos_.sideToMove()) == kInvalidCell) return EndReason::NoLegalMoves;
  return inCheck() ? EndReason::Checkmate : EndReason::Stalemate;
}

GameResult Game::result() const {
  const Color side = pos_.sideToMove();
  const Color other = opponent(side);
  switch (endReason()) {
    case EndReason::None:
      return GameResult::InProgress;
    case EndReason::Checkmate:
      return other == Color::White ? GameResult::WhiteWins : GameResult::BlackWins;
    case EndReason::Stalemate:
      switch (v_->stalemate) {
        case StalematePolicy::Draw:
          return GameResult::Draw;
        case StalematePolicy::Loss:
          return other == Color::White ? GameResult::WhiteWins : GameResult::BlackWins;
        case StalematePolicy::Win:
          return side == Color::White ? GameResult::WhiteWins : GameResult::BlackWins;
      }
      return GameResult::Draw;
    case EndReason::NoLegalMoves:
      // Being unable to move loses, which is what checkers and similar variants want.
      return other == Color::White ? GameResult::WhiteWins : GameResult::BlackWins;
    case EndReason::DrawClock:
    case EndReason::Repetition:
      return GameResult::Draw;
  }
  return GameResult::InProgress;
}

std::string Game::statusLine() const {
  std::string s = pos_.sideToMove() == Color::White ? "White" : "Black";
  s += " to move";
  const GameResult r = result();
  if (r != GameResult::InProgress) {
    s = std::string(toString(r));
    const EndReason why = endReason();
    if (why != EndReason::None) {
      s += " by ";
      s += toString(why);
    }
  } else if (inCheck()) {
    s += " (in check)";
  }
  s += "  -  ply " + std::to_string(plyCount());
  s += ", " + std::to_string(legalMoves().size()) + " legal move(s)";
  return s;
}

}  // namespace cb
