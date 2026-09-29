// SPDX-License-Identifier: GPL-3.0-or-later
#include "position/position.hpp"

#include <cassert>

namespace cb {

Position::Position(const VariantSpec& v)
    : v_(&v), cells_(v.dims.cellCount()), occAll_(v.dims.cellCount()) {
  assert(v.finalized() && "a variant must be finalized before a position uses it");
  for (auto& o : occ_) o.reset(v.dims.cellCount());
  side_ = v.startSideToMove;
  hash_ = computeHash();
}

Position Position::startPosition(const VariantSpec& v) {
  Position p(v);
  for (const StartPiece& sp : v.start) {
    p.place(v.dims.toCell(sp.at), sp.type, sp.color);
  }
  // Every declared castling right begins available; a loader may narrow it.
  for (const CastleTemplate& ct : v.castles) {
    p.castleRights_ = static_cast<std::uint8_t>(p.castleRights_ | (1u << ct.rightsBit));
  }
  p.side_ = v.startSideToMove;
  p.hash_ = p.computeHash();
  return p;
}

void Position::clear() {
  for (Piece& c : cells_) c = Piece{};
  for (auto& o : occ_) o.clear();
  occAll_.clear();
  ep_ = kInvalidCell;
  epVictim_ = kInvalidCell;
  castleRights_ = 0;
  halfmove_ = 0;
  fullmove_ = 1;
  hash_ = computeHash();
}

void Position::addPiece(CellId c, Piece p) {
  cells_[c] = p;
  occ_[p.color].set(c);
  occAll_.set(c);
  hash_ ^= v_->zob.piece(c, pieceCode(p.type, p.colorOf()));
}

void Position::removePiece(CellId c) {
  const Piece p = cells_[c];
  hash_ ^= v_->zob.piece(c, pieceCode(p.type, p.colorOf()));
  cells_[c] = Piece{};
  occ_[p.color].clearBit(c);
  occAll_.clearBit(c);
}

void Position::place(CellId c, PieceTypeId type, Color color) {
  if (!cells_[c].empty()) removePiece(c);
  addPiece(c, Piece{type, static_cast<std::uint8_t>(color), 0});
}

void Position::removeAt(CellId c) {
  if (!cells_[c].empty()) removePiece(c);
}

void Position::setSideToMove(Color c) {
  if (c != side_) {
    hash_ ^= v_->zob.side();
    side_ = c;
  }
}

void Position::setEpTarget(CellId target, CellId victim) {
  hash_ ^= v_->zob.epTarget(ep_ == kInvalidCell ? cellCount() : ep_);
  ep_ = target;
  epVictim_ = target == kInvalidCell ? kInvalidCell : victim;
  hash_ ^= v_->zob.epTarget(ep_ == kInvalidCell ? cellCount() : ep_);
}

void Position::setCastleRights(std::uint8_t mask) {
  hash_ ^= v_->zob.castleRights(castleRights_);
  castleRights_ = mask;
  hash_ ^= v_->zob.castleRights(castleRights_);
}

void Position::setClocks(std::int32_t halfmove, std::int32_t fullmove) {
  halfmove_ = halfmove;
  fullmove_ = fullmove;
}

std::uint64_t Position::computeHash() const {
  std::uint64_t h = 0;
  for (CellId c = 0; c < cells_.size(); ++c) {
    const Piece p = cells_[c];
    if (!p.empty()) h ^= v_->zob.piece(c, pieceCode(p.type, p.colorOf()));
  }
  if (side_ == Color::Black) h ^= v_->zob.side();
  h ^= v_->zob.epTarget(ep_ == kInvalidCell ? cellCount() : ep_);
  h ^= v_->zob.castleRights(castleRights_);
  return h;
}

void Position::make(const Move& m, Undo& u) {
  u.captured = Piece{};
  u.epBefore = ep_;
  u.epVictimBefore = epVictim_;
  u.castleRightsBefore = castleRights_;
  u.halfmoveBefore = halfmove_;
  u.hashBefore = hash_;

  const Piece mover = cells_[m.from];
  assert(!mover.empty() && "moving from an empty cell");

  bool resetClock = false;
  if (m.captureCell != kInvalidCell) {
    u.captured = cells_[m.captureCell];
    assert(!u.captured.empty() && "capturing an empty cell");
    removePiece(m.captureCell);
    resetClock = true;
  }

  removePiece(m.from);
  const PieceTypeId landing = has(m.flags, MoveFlag::Promotion) ? m.promoteTo : mover.type;
  addPiece(m.to, Piece{landing, mover.color, mover.flags});

  if (has(m.flags, MoveFlag::Castle)) {
    const CastleTemplate& ct = v_->castles[m.castleIndex];
    const Piece rook = cells_[ct.rookFrom];
    removePiece(ct.rookFrom);
    addPiece(ct.rookTo, rook);
  }

  if (v_->pieces[mover.type].resetsDrawClock) resetClock = true;

  // Castling rights are lost when the king or the rook leaves its post, or when
  // the rook is captured on it. Expressed against the templates, so a variant
  // with different castling loses rights correctly with no extra code.
  std::uint8_t rights = castleRights_;
  for (const CastleTemplate& ct : v_->castles) {
    const bool touched = m.from == ct.kingFrom || m.from == ct.rookFrom ||
                         m.to == ct.rookFrom || m.captureCell == ct.rookFrom;
    if (touched) rights = static_cast<std::uint8_t>(rights & ~(1u << ct.rightsBit));
  }
  if (rights != castleRights_) setCastleRights(rights);

  if (has(m.flags, MoveFlag::LeavesEnPassant)) {
    setEpTarget(m.epTarget, m.epVictim);
  } else {
    setEpTarget(kInvalidCell);
  }

  halfmove_ = resetClock ? 0 : halfmove_ + 1;
  if (side_ == Color::Black) ++fullmove_;
  setSideToMove(opponent(side_));
}

void Position::unmake(const Move& m, const Undo& u) {
  setSideToMove(opponent(side_));
  if (side_ == Color::Black) --fullmove_;

  if (has(m.flags, MoveFlag::Castle)) {
    const CastleTemplate& ct = v_->castles[m.castleIndex];
    const Piece rook = cells_[ct.rookTo];
    removePiece(ct.rookTo);
    addPiece(ct.rookFrom, rook);
  }

  const Piece landed = cells_[m.to];
  removePiece(m.to);
  const PieceTypeId original =
      has(m.flags, MoveFlag::Promotion)
          ? [&] {
              // The mover was whatever promotes into m.promoteTo; in every variant
              // so far that is unique per colour, and the move records enough.
              for (std::size_t i = 1; i < v_->pieces.size(); ++i) {
                for (PieceTypeId to : v_->pieces[i].promotesTo) {
                  if (to == m.promoteTo) return static_cast<PieceTypeId>(i);
                }
              }
              return landed.type;
            }()
          : landed.type;
  addPiece(m.from, Piece{original, landed.color, landed.flags});

  if (m.captureCell != kInvalidCell) addPiece(m.captureCell, u.captured);

  // Restoring these directly (rather than re-deriving) is what makes unmake
  // exact: the hash is restored, not recomputed, so a mismatch is detectable.
  ep_ = u.epBefore;
  epVictim_ = u.epVictimBefore;
  castleRights_ = u.castleRightsBefore;
  halfmove_ = u.halfmoveBefore;
  hash_ = u.hashBefore;
}

CellId Position::findRoyal(Color c) const {
  CellId found = kInvalidCell;
  occ_[static_cast<std::size_t>(c)].forEach([&](std::size_t i) {
    if (found == kInvalidCell && v_->pieces[cells_[i].type].royal) {
      found = static_cast<CellId>(i);
    }
  });
  return found;
}

bool Position::validate(std::string* why) const {
  const auto bad = [&](std::string msg) {
    if (why != nullptr) *why = std::move(msg);
    return false;
  };
  BitWords all(cellCount());
  BitWords per[kNumColors]{BitWords(cellCount()), BitWords(cellCount())};
  for (CellId c = 0; c < cells_.size(); ++c) {
    const Piece p = cells_[c];
    if (p.empty()) continue;
    if (p.type >= v_->pieces.size()) return bad("cell " + std::to_string(c) + " holds an unknown piece type");
    if (p.color >= kNumColors) return bad("cell " + std::to_string(c) + " holds an unknown colour");
    all.set(c);
    per[p.color].set(c);
  }
  if (!(all == occAll_)) return bad("occupancy disagrees with the cell array");
  for (int i = 0; i < kNumColors; ++i) {
    if (!(per[i] == occ_[i])) return bad("colour occupancy disagrees with the cell array");
  }
  if (hash_ != computeHash()) return bad("incremental hash disagrees with a fresh computation");
  return true;
}

}  // namespace cb
