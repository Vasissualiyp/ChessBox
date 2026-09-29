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
    case EndReason::RuleDeclared:
      return "a variant rule";
  }
  return "?";
}

namespace {

rules::RuleSet ruleSetOf(const VariantSpec& v) {
  if (!v.ruleSet) return {};
  return *static_cast<const rules::RuleSet*>(v.ruleSet.get());
}

}  // namespace

Game::Game(const VariantSpec& v)
    : v_(&v), pos_(Position::startPosition(v)), gen_(v), engine_(ruleSetOf(v)) {
  hashes_.push_back(pos_.hash());
  // Whether this variant has royal pieces at all is a property of the variant, not of
  // the position: a king blown off the board must make the move illegal rather than
  // quietly turn the game into one without kings.
  startHasRoyal_ = pos_.findRoyal(Color::White) != kInvalidCell ||
                   pos_.findRoyal(Color::Black) != kInvalidCell;
}

bool Game::hasCaptureFrom(CellId c) const {
  // kInvalidCell means "from anywhere", which is how a forced-capture rule asks whether
  // the side to move has any capture at all.
  MoveList list(v_->moveUpperBound());
  gen_.generatePseudoLegal(pos_, list);
  for (const Move& m : list) {
    if (m.captureCell == kInvalidCell) continue;
    if (c == kInvalidCell || m.from == c) return true;
  }
  return false;
}

rules::RuleEnv Game::makeEnv(const Move* m, Color mover) const {
  rules::RuleEnv env;
  env.pos = const_cast<Position*>(&pos_);
  env.move = m;
  env.mover = mover;
  env.hasCaptureFrom = [this](CellId c) { return hasCaptureFrom(c); };
  return env;
}

void Game::invalidate() {
  legalCacheValid_ = false;
}

const std::vector<Move>& Game::legalMoves() const {
  if (legalCacheValid_) return legalCache_;
  legalCacheValid_ = true;
  Position& p = const_cast<Position&>(pos_);

  if (engine_.empty()) {
    // No rules: royal safety is the only condition, and movegen decides it directly.
    MoveList list(v_->moveUpperBound());
    gen_.generateLegal(p, list);
    legalCache_.assign(list.begin(), list.end());
    return legalCache_;
  }

  // With rules in play, legality has to account for a move's *consequences*. In atomic
  // chess a capture that blows up your own king is illegal, and no amount of looking at
  // the board before the explosion will tell you that. So each candidate is played, its
  // effects are run, and only then is the royal piece examined.
  const Color mover = pos_.sideToMove();
  MoveList pseudo(v_->moveUpperBound());
  gen_.generatePseudoLegal(p, pseudo);

  legalCache_.clear();
  legalCache_.reserve(pseudo.size());
  for (const Move& m : pseudo) {
    rules::RuleEnv filterEnv = makeEnv(&m, mover);
    if (engine_.run(rules::Trigger::OnMoveFilter, filterEnv, nullptr).forbidden) continue;

    Undo u;
    p.make(m, u);
    rules::RuleEnv env = makeEnv(&m, mover);
    if (m.captureCell != kInvalidCell)
      (void)engine_.run(rules::Trigger::OnCapture, env, &u);
    (void)engine_.run(rules::Trigger::OnMoveEnd, env, &u);

    const CellId royal = p.findRoyal(mover);
    const bool hadRoyal = startHasRoyal_;
    // A variant with royals requires yours to survive and to be safe; a variant without
    // them (checkers and friends) has no such condition.
    const bool ok = !hadRoyal || (royal != kInvalidCell &&
                                  !gen_.isAttacked(p, royal, opponent(mover)));

    p.unmake(m, u);
    if (ok) legalCache_.push_back(m);
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
  const Color mover = pos_.sideToMove();
  Undo u;
  pos_.make(m, u);

  bool repeat = false;
  if (!engine_.empty()) {
    rules::RuleEnv env = makeEnv(&m, mover);
    rules::RuleOutcome outcome;
    if (m.captureCell != kInvalidCell) {
      outcome = engine_.run(rules::Trigger::OnCapture, env, &u);
    }
    const rules::RuleOutcome after = engine_.run(rules::Trigger::OnMoveEnd, env, &u);
    repeat = outcome.repeatTurn || after.repeatTurn;
    if (outcome.gameOver || after.gameOver) {
      ruleEnded_ = true;
      ruleOutcome_ = outcome.gameOver ? outcome.outcome : after.outcome;
      ruleOutcomeMover_ = mover;
    }
    if (repeat) {
      // Hand the turn back. Done through setSideToMove so the hash stays consistent,
      // and reversed the same way in undo().
      pos_.setSideToMove(mover);
    }
    rules::RuleEnv turnEnv = makeEnv(&m, mover);
    (void)engine_.run(rules::Trigger::OnTurnEnd, turnEnv, &u);
  }

  history_.push_back(std::move(u));
  played_.push_back(m);
  repeated_.push_back(repeat);
  hashes_.push_back(pos_.hash());
  invalidate();
  return {};
}

bool Game::undo() {
  if (history_.empty()) return false;
  // Reverse the turn-repeat flip first, so unmake's own side flip lands where it
  // expects to and the hash comes back exactly.
  if (repeated_.back()) pos_.setSideToMove(opponent(pos_.sideToMove()));
  pos_.unmake(played_.back(), history_.back());
  history_.pop_back();
  played_.pop_back();
  repeated_.pop_back();
  hashes_.pop_back();
  ruleEnded_ = false;
  invalidate();
  return true;
}

void Game::reset() {
  pos_ = Position::startPosition(*v_);
  history_.clear();
  played_.clear();
  repeated_.clear();
  ruleEnded_ = false;
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
  if (ruleEnded_) return EndReason::RuleDeclared;
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
  if (ruleEnded_) {
    // A rule's outcome is stated relative to whoever made the move that triggered it.
    switch (ruleOutcome_) {
      case rules::Outcome::MoverWins:
        return ruleOutcomeMover_ == Color::White ? GameResult::WhiteWins
                                                 : GameResult::BlackWins;
      case rules::Outcome::MoverLoses:
        return ruleOutcomeMover_ == Color::White ? GameResult::BlackWins
                                                 : GameResult::WhiteWins;
      case rules::Outcome::Draw:
        return GameResult::Draw;
    }
  }
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
    case EndReason::RuleDeclared:
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
