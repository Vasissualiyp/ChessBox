// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session.hpp"

#include <algorithm>
#include <sstream>

#include "io/fen.hpp"
#include "io/notation.hpp"

namespace cb::app {
namespace {

/// Ease the shot in and out at the ends of a move, so a camera that only leads for the
/// middle of the move does not snap onto the piece at the first frame or back off at the
/// last. 1 through the body of the move, 0 at both ends.
float shotEnvelope(float progress) noexcept {
  const float in = std::clamp(progress / 0.15f, 0.0f, 1.0f);
  const float out = std::clamp((1.0f - progress) / 0.15f, 0.0f, 1.0f);
  const float e = std::min(in, out);
  return e * e * (3.0f - 2.0f * e);
}

/// Shortest signed difference between two angles, so a shot offset that wraps from just
/// under pi to just over it does not spin the camera the long way round.
float wrapAngle(float a) noexcept {
  constexpr float kPi = 3.14159265358979f;
  constexpr float kTwoPi = 2.0f * kPi;
  while (a > kPi) a -= kTwoPi;
  while (a < -kPi) a += kTwoPi;
  return a;
}

}  // namespace

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
  if (verb == "confirm") {
    a.kind = ActionKind::Confirm;
    return a;
  }
  if (verb == "cancel") {
    a.kind = ActionKind::Cancel;
    return a;
  }
  if (verb == "resign") {
    a.kind = ActionKind::Resign;
    return a;
  }
  if (verb == "draw") {
    a.kind = ActionKind::AgreeDraw;
    return a;
  }
  if (verb == "orbit") {
    a.kind = ActionKind::Orbit;
    in >> a.dx >> a.dy;
    return a;
  }
  if (verb == "pan") {
    a.kind = ActionKind::Pan;
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
    std::string mode;
    if (in >> mode && mode == "vertical") a.gridVertical = true;
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
  s->chosenCfg_ = view::ViewConfig::forBoard(s->variant_->dims);
  if (auto ok = s->chosenCfg_.validate(s->variant_->dims); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message);
  }
  s->refreshView();
  s->refreshSnapshot();
  return s;
}

view::ViewConfig Session::effectiveViewConfig() const {
  view::ViewConfig cfg = chosenCfg_;
  if (!flat_) return cfg;
  // A depth axis is invisible from straight above: the layers would sit exactly on top
  // of one another and only the top board would show. Demote any third screen axis to a
  // grid axis so the boards spread across the screen instead, and never stack them.
  if (cfg.screenAxes.size() > 2) {
    SmallVec<std::uint8_t, 3> screen;
    SmallVec<std::uint8_t, kMaxDims> grid;
    for (std::size_t i = 0; i < cfg.screenAxes.size(); ++i) {
      if (i < 2) {
        screen.push(cfg.screenAxes[i]);
      } else {
        grid.push(cfg.screenAxes[i]);
      }
    }
    for (std::uint8_t a : cfg.gridAxes) grid.push(a);
    cfg.screenAxes = screen;
    cfg.gridAxes = grid;
  }
  cfg.gridVertical = false;
  return cfg;
}

bool Session::boardVisible(CellId c) const {
  return game_ == nullptr || game_->boardVisible(c);
}

std::vector<view::TimelineLink> Session::timelineLinks() const {
  std::vector<view::TimelineLink> out;
  if (game_ == nullptr) return out;
  const temporal::Multiverse* m = game_->multiverse();
  if (m == nullptr) return out;
  for (const temporal::Timeline& t : m->timelines().timelines()) {
    if (t.parentTurn < 0) continue;  // the original timeline came from nowhere
    out.push_back(view::TimelineLink{t.parentLine, t.id, t.parentTurn});
  }
  return out;
}

void Session::refreshView() {
  viewCfg_ = effectiveViewConfig();
  placements_ = view::layout(variant_->dims, viewCfg_);
  // A temporal variant's lattice is mostly space the multiverse has not filled yet:
  // show only the boards that exist, so the game opens on one board, not a grid of
  // empty ones.
  placements_.erase(
      std::remove_if(placements_.begin(), placements_.end(),
                     [&](const view::Placement& p) { return !boardVisible(p.cell); }),
      placements_.end());
  seams_ = view::SeamMap::build(*variant_, viewCfg_, theme_);
  // Framed for the shape of the area the board is drawn into, which the interface
  // narrows with its rails - not for the whole window.
  camera_ = view::OrbitCamera::frame(view::boundsOf(placements_), boardAspect_);
  applyViewMode();
}

void Session::refreshTemporalView() {
  if (game_ == nullptr || !game_->isTemporal()) return;
  const float yaw = camera_.yaw;
  const float pitch = camera_.pitch;
  const float distance = camera_.distance;
  refreshView();
  // Center on the board(s) to answer on, so the newly created board the player must move
  // on is in view rather than off to the side. The zoom is the player's: a new board must
  // not pull the camera back, or every turn reads as a zoom-out.
  float cx = 0.0f;
  float cy = 0.0f;
  float cz = 0.0f;
  int n = 0;
  for (const view::Placement& p : placements_) {
    if (game_->cellInPresent(p.cell)) {
      cx += p.x;
      cy += p.y;
      cz += p.z;
      ++n;
    }
  }
  if (n > 0)
    camera_.target = view::Vec3{cx / static_cast<float>(n), cy / static_cast<float>(n),
                                cz / static_cast<float>(n)};
  if (!flat_) {
    camera_.yaw = yaw;
    camera_.pitch = pitch;
  }
  camera_.distance = distance;
}

void Session::setTheme(const view::Theme& t) {
  theme_ = t;
  // The seam colours come out of the palette, so they are rebuilt with it.
  seams_ = view::SeamMap::build(*variant_, viewCfg_, theme_);
}

void Session::setFlatView(bool flat) {
  if (flat == flat_) return;
  flat_ = flat;
  // The layout itself changes: flat mode moves a depth axis out of the way, so the
  // board has to be re-laid and re-framed, not merely re-aimed.
  refreshView();
}

bool Session::feedHotSeat(char key) {
  if (!hotSeatEnabled_ || game_ == nullptr) return false;
  const bool cancel = key == HotSeat::whiteCancel() || key == HotSeat::blackCancel();
  if (!cancel && !HotSeat::ownerOf(key).has_value()) return false;

  const std::optional<CellId> square =
      hotSeatKeys_.feed(key, variant_->dims, game_->position().sideToMove());
  if (square.has_value()) {
    Action a;
    a.kind = ActionKind::ClickCell;
    a.cell = *square;
    (void)apply(a);
  }
  return true;
}

void Session::applyViewMode() {
  camera_.orthographic = flat_;
  if (flat_) {
    // Straight down. The camera's own up-vector fallback handles the degenerate basis,
    // so this needs no nudge off the pole to avoid a singularity.
    camera_.pitch = 1.5707963f;
    camera_.yaw = 0.0f;
  } else {
    // Coming back from a flat view the pitch is still at the pole; pull it into the
    // solid range so the board is read from above rather than edge-on or underneath.
    camera_.pitch = std::clamp(camera_.pitch, 0.05f, 1.5207963f);
  }
}

view::OrbitCamera Session::camera() const noexcept {
  view::OrbitCamera out = camera_;
  out.distance *= 1.0f + 0.18f * pullBack_;
  if (!shotInFlight()) return out;

  // The move camera is a time-varying contribution to this one accessor, so the board,
  // the picking ray and every label drawn over it agree by construction - which is why it
  // is folded in here rather than kept as a second camera elsewhere.
  //
  // The shot is applied as an **offset in its own frame**: the pose it would use with
  // following off is subtracted first, so the player's own orbit, pan and zoom survive
  // and the move camera only leads them. A player who turned the board keeps that turn.
  const view::Bounds scene = view::boundsOf(placements_);
  view::CameraPolicy off = cameraPolicy_;
  off.follow = view::FollowMode::Off;
  const view::CameraPose settled =
      view::moveCamera(lastPath_, placements_, viewCfg_, scene, off, 1.0f);
  const view::CameraPose shot = view::moveCamera(lastPath_, placements_, viewCfg_, scene,
                                                 cameraPolicy_, anim_.progress());
  const float k = followStrength_ * shotEnvelope(anim_.progress());
  out.target = out.target + (shot.target - settled.target) * k;
  out.yaw = out.yaw + wrapAngle(shot.yaw - settled.yaw) * k;
  out.pitch = std::clamp(out.pitch + (shot.pitch - settled.pitch) * k, 0.10f, 1.53f);
  out.distance = std::max(1.0f, out.distance + (shot.distance - settled.distance) * k);
  return out;
}

bool Session::advanceAnimation(float dt) {
  anim_.advance(dt);
  if (!anim_.active()) pathValid_ = false;
  return anim_.active();
}

void Session::setCameraMode(std::string_view mode) noexcept {
  if (mode == "piece") {
    cameraPolicy_.follow = view::FollowMode::Piece;
  } else if (mode == "route") {
    cameraPolicy_.follow = view::FollowMode::Route;
  } else {
    cameraPolicy_.follow = view::FollowMode::Off;  // the default, and the pre-M11 look
  }
}

bool Session::shotInFlight() const noexcept {
  return pathValid_ && anim_.active() && cameraPolicy_.follow != view::FollowMode::Off &&
         followStrength_ > 0.0f;
}

void Session::setBoardAspect(float aspect) {
  if (aspect <= 0.0f || std::abs(aspect - boardAspect_) < 0.01f) return;
  boardAspect_ = aspect;
  const view::Vec3 keptTarget = camera_.target;
  const float keptDistance = camera_.distance;
  const float keptYaw = camera_.yaw;
  const float keptPitch = camera_.pitch;
  camera_ = view::OrbitCamera::frame(view::boundsOf(placements_), boardAspect_);
  // A resize re-frames, but whatever the player had turned, panned or zoomed to is
  // theirs to keep. A temporal board has just been centered on the present board, so it
  // keeps that target even on the first framing.
  if (framedOnce_ || (game_ != nullptr && game_->isTemporal())) {
    camera_.target = keptTarget;
    camera_.distance = keptDistance;
    camera_.yaw = keptYaw;
    camera_.pitch = keptPitch;
  }
  framedOnce_ = true;
  applyViewMode();
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
  // The route is traced against the position the piece set off from, before the move is
  // applied: occupancy decides which way round a glued board the piece actually went,
  // and after play() the blocker it avoided and the mover itself are both gone.
  const Piece mover = game_->position().at(chosen.from);
  view::MovePath path;
  const bool animating = animSeconds_ > 0.0f && !mover.empty();
  if (animating) {
    path = view::tracePath(*variant_, game_->position(), mover.type, mover.colorOf(),
                           chosen);
  }
  if (auto ok = game_->play(chosen); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message);
  }
  if (animating) {
    anim_.start(viewCfg_, placements_, seams_, theme_, path, animSeconds_);
    // The camera reads the same route the animation draws, so the two cannot disagree
    // about which way round a glued board the piece went.
    lastPath_ = path;
    pathValid_ = true;
  } else {
    anim_.clear();
    pathValid_ = false;
  }
  message_ = moveText(*variant_, chosen);
  selected_ = kInvalidCell;
  pending_ = PendingPromotion{};
  refreshSnapshot();
  refreshTemporalView();
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

Result<void> Session::confirmMove() {
  if (!pendingMove_.active) {
    return fail(ErrorCode::Internal, "no move is waiting for confirmation");
  }
  const CellId from = pendingMove_.from;
  const CellId to = pendingMove_.to;
  pendingMove_ = PendingMove{};
  const Move* m = findMove(from, to);
  if (m == nullptr) {
    message_ = "that move is no longer legal";
    selected_ = kInvalidCell;
    refreshSnapshot();
    return fail(ErrorCode::ValidationError, "that move is no longer legal");
  }
  return playChecked(*m);
}

void Session::cancelMove() {
  if (!pendingMove_.active) return;
  pendingMove_ = PendingMove{};
  message_ = "cancelled";
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
      if (pending_.active || pendingMove_.active) return {};
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
          if (confirmMoves_) {
            pendingMove_ = PendingMove{true, selected_, a.cell};
            message_ = "confirm the move?";
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

    case ActionKind::Resign:
      game_->resign(game_->position().sideToMove());
      selected_ = kInvalidCell;
      message_ = "resigned";
      refreshSnapshot();
      return {};

    case ActionKind::AgreeDraw:
      game_->agreeDraw();
      selected_ = kInvalidCell;
      message_ = "a draw was agreed";
      refreshSnapshot();
      return {};

    case ActionKind::Confirm:
      return confirmMove();

    case ActionKind::Cancel:
      cancelMove();
      return {};

    case ActionKind::Undo:
      pending_ = PendingPromotion{};
      pendingMove_ = PendingMove{};
      if (!game_->undo()) {
        message_ = "nothing to undo";
        return {};
      }
      selected_ = kInvalidCell;
      message_ = "undone";
      refreshSnapshot();
      refreshTemporalView();
      return {};

    case ActionKind::Reset:
      game_->reset();
      pending_ = PendingPromotion{};
      pendingMove_ = PendingMove{};
      selected_ = kInvalidCell;
      message_ = "reset";
      refreshSnapshot();
      refreshTemporalView();
      return {};

    case ActionKind::Orbit:
      camera_.yaw += a.dx;
      // A flat view is always straight down. A solid one may tilt, but never so far as
      // to look up at the board's underside: elevation stays strictly between the
      // horizon and the pole, so the top face is always the one being read.
      if (flat_) {
        camera_.pitch = 1.5707963f;
      } else {
        camera_.pitch = std::clamp(camera_.pitch + a.dy, 0.05f, 1.5207963f);
      }
      return {};

    case ActionKind::Pan: {
      // Slide the look-at point in the plane the camera is facing, so the board moves
      // with the mouse and the viewing angle does not change.
      const view::Vec3 forward = view::normalize(camera_.target - camera_.eye());
      const view::Vec3 right = view::normalize(view::cross(forward, camera_.upHint()));
      const view::Vec3 up = view::cross(right, forward);
      camera_.target = camera_.target + right * a.dx + up * a.dy;
      return {};
    }

    case ActionKind::Zoom:
      if (a.dx > 0)
        camera_.distance = std::clamp(camera_.distance * a.dx, 1.0f, 10000.0f);
      return {};

    case ActionKind::SetScreenAxes: {
      view::ViewConfig cfg;
      if (a.text == "default") {
        // The board's own view: every axis it can draw, with the rest as a grid.
        cfg = view::ViewConfig::forBoard(variant_->dims);
      } else {
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
        cfg.gridVertical = a.gridVertical;
      }
      if (auto ok = cfg.validate(variant_->dims); !ok.has_value()) {
        return fail(ok.error().code, ok.error().message);
      }
      chosenCfg_ = cfg;
      refreshView();
      message_ = a.text == "default" ? "view: default" : "view: " + a.text;
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
  // While a shot owns the view the board is locked: the pose is moving, so a pixel would
  // resolve against where the board *was* and select the wrong cell. When the shot
  // settles, picking resumes from the settled pose - no invisible drift.
  if (shotInFlight()) return kInvalidCell;
  // The effective camera, not the raw member: the pause pull-back moves the board, and
  // the ray has to move with it or a click lands beside the cell under the cursor.
  const auto ray = camera().pickRay(px, py, width, height);
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
