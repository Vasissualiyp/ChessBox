// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session.hpp"

#include <algorithm>
#include <sstream>

#include "io/fen.hpp"
#include "io/notation.hpp"

namespace cb::app {

Result<Action> parseAction(const VariantSpec& v, std::string_view line) {
  std::istringstream in{std::string(line)};
  std::string verb;
  in >> verb;
  Action a;
  if (verb.empty() || verb[0] == '#') return a;  // blank or comment

  if (verb == "click") {
    std::string where;
    in >> where;
    auto cell = parseCell(v.dims, where);
    if (!cell.has_value()) return fail(cell.error().code, cell.error().message);
    a.kind = ActionKind::ClickCell;
    a.cell = *cell;
    return a;
  }
  if (verb == "undo") {
    a.kind = ActionKind::Undo;
    return a;
  }
  if (verb == "reset") {
    a.kind = ActionKind::Reset;
    return a;
  }
  if (verb == "orbit") {
    a.kind = ActionKind::Orbit;
    in >> a.dx >> a.dy;
    return a;
  }
  if (verb == "zoom") {
    a.kind = ActionKind::Zoom;
    in >> a.dx;
    return a;
  }
  if (verb == "axes") {
    a.kind = ActionKind::SetScreenAxes;
    in >> a.text;
    return a;
  }
  if (verb == "promote") {
    a.kind = ActionKind::SetPromotion;
    in >> a.text;
    return a;
  }
  return fail(ErrorCode::ParseError, "unknown action '" + verb + "'");
}

Result<std::unique_ptr<Session>> Session::create(VariantSpec variant) {
  if (!variant.finalized()) {
    return fail(ErrorCode::Internal, "a session needs a finalized variant");
  }
  auto s = std::unique_ptr<Session>(new Session());
  s->variant_ = std::make_unique<VariantSpec>(std::move(variant));
  s->game_ = std::make_unique<Game>(*s->variant_);
  s->viewCfg_ = view::ViewConfig::forBoard(s->variant_->dims);
  if (auto ok = s->viewCfg_.validate(s->variant_->dims); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message);
  }
  s->refreshView();
  s->refreshSnapshot();
  return s;
}

void Session::refreshView() {
  placements_ = view::layout(variant_->dims, viewCfg_);
  // Framed for the shape of the area the board is drawn into, which the interface
  // narrows with its rails - not for the whole window.
  camera_ = view::OrbitCamera::frame(view::boundsOf(placements_), boardAspect_);
}

void Session::setBoardAspect(float aspect) {
  if (aspect <= 0.0f || std::abs(aspect - boardAspect_) < 0.01f) return;
  boardAspect_ = aspect;
  const float keptDistance = camera_.distance;
  const float keptYaw = camera_.yaw;
  const float keptPitch = camera_.pitch;
  camera_ = view::OrbitCamera::frame(view::boundsOf(placements_), boardAspect_);
  // A resize re-frames, but whatever the player had turned or zoomed to is theirs to
  // keep unless the board itself changed.
  if (framedOnce_) {
    camera_.distance = keptDistance;
    camera_.yaw = keptYaw;
    camera_.pitch = keptPitch;
  }
  framedOnce_ = true;
}

const Move* Session::findMove(CellId from, CellId to) const {
  const Move* best = nullptr;
  for (const Move& m : game_->legalMoves()) {
    if (m.from != from || m.to != to) continue;
    if (best == nullptr) {
      best = &m;
      continue;
    }
    // Several moves can share from/to: the promotion choices. Prefer the piece the user
    // asked for, and otherwise the last one declared, which is the strongest by
    // convention in every variant that ships here.
    if (!promotionPreference_.empty() && m.promoteTo != kNoPiece &&
        variant_->pieces[m.promoteTo].name == promotionPreference_) {
      best = &m;
    } else if (promotionPreference_.empty() && m.promoteTo > best->promoteTo) {
      best = &m;
    }
  }
  return best;
}

std::vector<PieceTypeId> Session::promotionChoicesFor(CellId from, CellId to) const {
  std::vector<PieceTypeId> out;
  for (const Move& m : game_->legalMoves()) {
    if (m.from != from || m.to != to || m.promoteTo == kNoPiece) continue;
    if (std::find(out.begin(), out.end(), m.promoteTo) == out.end())
      out.push_back(m.promoteTo);
  }
  return out;
}

Result<void> Session::playChecked(const Move& m) {
  const Move chosen = m;  // play() invalidates the legal-move cache the move points into
  if (auto ok = game_->play(chosen); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message);
  }
  message_ = moveText(*variant_, chosen);
  selected_ = kInvalidCell;
  pending_ = PendingPromotion{};
  refreshSnapshot();
  return {};
}

Result<void> Session::choosePromotion(PieceTypeId piece) {
  if (!pending_.active) return fail(ErrorCode::Internal, "no promotion is pending");
  for (const Move& m : game_->legalMoves()) {
    if (m.from == pending_.from && m.to == pending_.to && m.promoteTo == piece) {
      return playChecked(m);
    }
  }
  return fail(ErrorCode::ValidationError, "that is not one of the promotion choices");
}

void Session::cancelPromotion() {
  pending_ = PendingPromotion{};
  selected_ = kInvalidCell;
  message_ = "promotion cancelled";
  refreshSnapshot();
}

void Session::refreshSnapshot() {
  snapshot_ = view::PositionView::capture(game_->position());
  snapshot_.setSelected(selected_);

  std::vector<CellId> targets;
  if (selected_ != kInvalidCell) {
    for (const Move& m : game_->legalMoves()) {
      if (m.from == selected_) targets.push_back(m.to);
    }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
  }
  snapshot_.setHighlighted(std::move(targets));
}

Result<void> Session::apply(const Action& a) {
  switch (a.kind) {
    case ActionKind::None:
      return {};

    case ActionKind::ClickCell: {
      if (a.cell >= variant_->dims.cellCount()) {
        return fail(ErrorCode::OutOfRange, "clicked cell is off the board");
      }
      // While a promotion is pending the board is frozen: the player has already
      // committed to the move and owes only the choice of piece.
      if (pending_.active) return {};
      const Piece piece = game_->position().at(a.cell);
      const bool ownPiece =
          !piece.empty() && piece.colorOf() == game_->position().sideToMove();

      if (selected_ != kInvalidCell) {
        if (const Move* m = findMove(selected_, a.cell); m != nullptr) {
          auto choices = promotionChoicesFor(selected_, a.cell);
          if (choices.size() > 1 && promotionPreference_.empty()) {
            // More than one thing the pawn could become and no standing preference:
            // ask, rather than picking for the player.
            pending_ = PendingPromotion{true, selected_, a.cell, std::move(choices)};
            message_ = "choose a piece";
            refreshSnapshot();
            return {};
          }
          return playChecked(*m);
        }
        if (a.cell == selected_) {
          selected_ = kInvalidCell;
          message_ = "deselected";
          refreshSnapshot();
          return {};
        }
      }

      if (ownPiece) {
        selected_ = a.cell;
        message_ = "selected " + cellName(variant_->dims, a.cell);
      } else {
        selected_ = kInvalidCell;
        // Clicking an empty cell or an enemy piece with nothing selected is an ordinary
        // part of using a board, not an error.
        message_ = piece.empty() ? "" : "that piece is not yours to move";
      }
      refreshSnapshot();
      return {};
    }

    case ActionKind::Undo:
      pending_ = PendingPromotion{};
      if (!game_->undo()) {
        message_ = "nothing to undo";
        return {};
      }
      selected_ = kInvalidCell;
      message_ = "undone";
      refreshSnapshot();
      return {};

    case ActionKind::Reset:
      game_->reset();
      pending_ = PendingPromotion{};
      selected_ = kInvalidCell;
      message_ = "reset";
      refreshSnapshot();
      return {};

    case ActionKind::Orbit:
      camera_.yaw += a.dx;
      camera_.pitch = std::clamp(camera_.pitch + a.dy, -1.5f, 1.5f);
      return {};

    case ActionKind::Zoom:
      if (a.dx > 0)
        camera_.distance = std::clamp(camera_.distance * a.dx, 1.0f, 10000.0f);
      return {};

    case ActionKind::SetScreenAxes: {
      view::ViewConfig cfg;
      std::stringstream ss(a.text);
      std::string name;
      std::vector<bool> used(variant_->dims.dims(), false);
      while (std::getline(ss, name, ',')) {
        const int idx = variant_->dims.axisIndex(name);
        if (idx < 0) {
          return fail(ErrorCode::ValidationError, "no axis is called '" + name + "'");
        }
        cfg.screenAxes.push(static_cast<std::uint8_t>(idx));
        used[static_cast<std::size_t>(idx)] = true;
      }
      for (std::uint8_t ax = 0; ax < variant_->dims.dims(); ++ax) {
        if (!used[ax]) cfg.gridAxes.push(ax);
      }
      if (auto ok = cfg.validate(variant_->dims); !ok.has_value()) {
        return fail(ok.error().code, ok.error().message);
      }
      viewCfg_ = cfg;
      refreshView();
      message_ = "view: " + a.text;
      return {};
    }

    case ActionKind::SetPromotion:
      if (variant_->findPiece(a.text) == kNoPiece) {
        return fail(ErrorCode::ValidationError, "no piece is called '" + a.text + "'");
      }
      promotionPreference_ = a.text;
      message_ = "promoting to " + a.text;
      return {};
  }
  return {};
}

Result<void> Session::loadFen(std::string_view fen) {
  auto p = fromFen(*variant_, fen);
  if (!p.has_value()) return fail(p.error().code, p.error().message);
  game_ = std::make_unique<Game>(*variant_);
  game_->position() = std::move(*p);
  selected_ = kInvalidCell;
  message_ = "position set";
  refreshSnapshot();
  return {};
}

CellId Session::clickPixel(float px, float py, float width, float height) {
  const auto ray = camera_.pickRay(px, py, width, height);
  const int hit = view::pickBox(ray, placements_, view::Vec3{0.45f, 0.45f, 0.06f});
  if (hit < 0) {
    selected_ = kInvalidCell;
    refreshSnapshot();
    return kInvalidCell;
  }
  const CellId cell = placements_[static_cast<std::size_t>(hit)].cell;
  Action a;
  a.kind = ActionKind::ClickCell;
  a.cell = cell;
  (void)apply(a);
  return cell;
}

Result<void> Session::applyScript(std::string_view script) {
  std::istringstream in{std::string(script)};
  std::string line;
  int lineNumber = 0;
  while (std::getline(in, line)) {
    ++lineNumber;
    auto action = parseAction(*variant_, line);
    if (!action.has_value()) {
      return fail(action.error().code, action.error().message, lineNumber);
    }
    if (auto ok = apply(*action); !ok.has_value()) {
      return fail(ok.error().code, ok.error().message, lineNumber);
    }
  }
  return {};
}

std::string Session::statusLine() const {
  std::string s = game_->statusLine();
  if (!message_.empty()) s += "  |  " + message_;
  return s;
}

}  // namespace cb::app
