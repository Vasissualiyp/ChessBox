// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/shape_sequence.hpp"

#include <algorithm>
#include <cmath>

#include "view/move_camera.hpp"

namespace cb::render {
namespace {

/// The base lengths of the choreography's three camera stages, in seconds, at morph speed
/// 1.0; `Settings::shapeMorphSpeed` divides them, so the slider slows or hurries the
/// whole lead-in and return without touching the move itself. The travel's own length is
/// the move animation's.
constexpr float kShapeAlignSeconds = 0.7f;
constexpr float kShapeApproachSeconds = 0.9f;
constexpr float kShapeReturnSeconds = 0.9f;
/// How fast the board may morph while a move is followed, in lattice cells of slide per
/// second at morph speed 1. A target half a period away is then a turn the eye can follow
/// rather than a flip - which is why the morph is a bounded step and not an eased jump
/// (M17.19).
constexpr float kShapeMorphRate = 6.0f;
/// How far ahead of the piece the morph looks for a camera that is about to cross the
/// board, as a fraction of the move. A small window is enough: the camera moves a little
/// each frame, and the board must already be turning by the time it gets there.
constexpr float kShapeLookahead = 0.06f;

/// A ceiling on the settle walk, so a pathological sequence cannot spin forever. Far
/// longer than any real align/return offset needs at the bounded morph rate.
constexpr float kShapeSettleCapSeconds = 30.0f;

/// A surface at the settings' current pose - the cache's entry when one is given, a fresh
/// build otherwise. The shared pointer keeps a handed-out surface alive even if the cache
/// is cleared underneath it.
std::shared_ptr<const PlaySurface> currentSurface(const app::Shell& shell,
                                                  SurfaceCache* cache) {
  const app::Session* session = shell.session();
  if (session == nullptr) return nullptr;
  const SurfacePose pose = surfacePose(shell.settings(), session->variant());
  if (cache != nullptr) return cache->get(session->variant(), pose);
  return std::make_shared<const PlaySurface>(
      PlaySurface::build(session->variant(), pose));
}

/// Move one slide coordinate toward `goal` by at most `maxStep`, the short way round the
/// `period` (0 when the axis does not wrap). The result is brought back into [0, period).
float stepSlideCoord(float cur, float goal, float maxStep, float period) {
  if (period <= 0.0f) {
    const float d = goal - cur;
    if (std::abs(d) <= maxStep) return goal;
    return cur + (d > 0.0f ? maxStep : -maxStep);
  }
  float d = std::fmod(goal - cur, period);
  if (d > period * 0.5f) d -= period;
  if (d < -period * 0.5f) d += period;
  if (std::abs(d) <= maxStep) return std::fmod(cur + d + period, period);
  float out = std::fmod(cur + (d > 0.0f ? maxStep : -maxStep), period);
  if (out < 0.0f) out += period;
  return out;
}

SlideOffset stepSlide(const SlideOffset& cur, const SlideOffset& goal, float maxStepU,
                      float maxStepV, float periodU, float periodV) {
  return {stepSlideCoord(cur.u, goal.u, maxStepU, periodU),
          stepSlideCoord(cur.v, goal.v, maxStepV, periodV)};
}

/// Run the fixed-step simulation `steps` times from `seq`, returning the sequence.
void stepTimes(app::Shell& shell, ShapeMoveSequence& seq, int steps,
               SurfaceCache* cache) {
  for (int i = 0; i < steps; ++i) {
    stepShapeSequenceOnce(shell, seq, kShapeSimStep, cache);
  }
}

}  // namespace

SurfacePose surfacePose(const app::Settings& s, const VariantSpec& v) {
  SurfacePose pose;
  pose.slideU = s.geometrySlideU + s.geometryAlignOffset;
  pose.slideV = s.geometrySlideV + s.geometryAlignOffsetV;
  pose.evert = s.geometryEvert;
  pose.twist = s.kleinTwist;
  pose.openness = s.geometryWidth;
  pose.thickness = s.geometryThickness;
  const float nx = static_cast<float>(v.dims.extent(0));
  pose.collapsePhase = nx > 0.0f ? s.kleinShift / nx : 0.0f;
  return pose;
}

bool shapeFollowApplies(const app::Shell& shell) {
  const app::Session* session = shell.session();
  if (session == nullptr) return false;
  const app::Settings& st = shell.settings();
  return shell.showsBoard() && st.geometryView && hasPlaySurface(session->variant()) &&
         session->followsMove();
}

float shapeLeadSeconds(const app::Settings& st) {
  const float speed = std::clamp(st.shapeMorphSpeed, 0.25f, 4.0f);
  return (kShapeAlignSeconds + kShapeApproachSeconds + kShapeReturnSeconds) / speed;
}

float chaseEyeDistance(const PlaySurface& surf) {
  const view::Bounds b = surf.bounds();
  const float span = std::max({b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ});
  return std::max(2.0f, 0.7f * span);
}

float followLift(const app::Settings& s) {
  constexpr float kPi = 3.14159265358979f;
  return std::tan(s.followElevationDeg * kPi / 180.0f);
}

CellId currentFollowedCell(const app::Shell& shell, const PlaySurface& surf) {
  const app::Session* session = shell.session();
  if (session == nullptr || !session->shotInFlight()) return kInvalidCell;
  const view::Vec3 here = surfaceMoveSample(session->animation().path(), surf,
                                            session->animation().progress())
                              .position;
  CellId best = kInvalidCell;
  float bestDist = 1e30f;
  for (const SurfaceSeat& s : surf.seats()) {
    const float d = view::length(s.centre - here);
    if (d < bestDist) {
      bestDist = d;
      best = s.cell;
    }
  }
  return best;
}

SlideOffset alignOffsetFor(const app::Shell& shell, CellId followed,
                           SurfaceCache* cache) {
  const app::Session* session = shell.session();
  const app::Settings& st = shell.settings();
  if (session == nullptr || followed == kInvalidCell || !st.geometryAlign ||
      !session->shotInFlight()) {
    return {};
  }
  if (st.shapeFollow == "chase") {
    const std::shared_ptr<const PlaySurface> surf = currentSurface(shell, cache);
    if (surf == nullptr) return {};
    const view::MovePath& path = session->animation().path();
    const float t = session->animation().progress();
    // Hysteresis: feed the offset drawn last frame back in, so the search keeps a
    // still-clear rotation instead of jumping to whatever marginally wins this frame
    // (M17.20).
    const SlideOffset hint{st.geometryAlignOffset, st.geometryAlignOffsetV};
    return alignSlideU(session->variant(), followed, path, t, chaseEyeDistance(*surf),
                       followLift(st), &hint);
  }
  const view::OrbitCamera base = session->playerCamera();
  const view::Vec3 toCamera = view::normalize(base.eye() - base.target);
  return alignSlideToFace(session->variant(), followed, toCamera, base.distance);
}

std::shared_ptr<const PlaySurface> SurfaceCache::get(const VariantSpec& v,
                                                     const SurfacePose& pose) {
  const Key key{&v,          pose.slideU,       pose.slideV, pose.evert,
                pose.twist,  pose.openness,     pose.stretch, pose.thickness,
                pose.collapsePhase};
  for (auto& [k, surf] : entries_) {
    if (k == key) return surf;
  }
  auto surf = std::make_shared<const PlaySurface>(PlaySurface::build(v, pose));
  entries_.emplace_back(key, surf);
  return surf;
}

ShapeMoveSequence beginShapeSequence(app::Shell& shell, const view::MovePath& path,
                                     float travelSeconds, const view::OrbitCamera& from,
                                     SurfaceCache* cache) {
  app::Session* session = shell.session();
  app::Settings& st = shell.settings();
  ShapeMoveSequence fresh;
  fresh.active = true;
  fresh.travelSeconds = std::max(1e-3f, travelSeconds);
  fresh.tokenFrom = path.from;
  fresh.tokenTo = path.to;
  fresh.startCam = from;
  fresh.camera = from;
  fresh.startOffset = {st.geometryAlignOffset, st.geometryAlignOffsetV};
  fresh.offset = fresh.startOffset;
  // Morph at the start only if the player's own line to the piece is actually blocked by
  // a square - the morph exists to clear a blocked view, not to turn a clear one.
  if (session != nullptr) {
    const std::shared_ptr<const PlaySurface> startSurf = currentSurface(shell, cache);
    if (startSurf != nullptr) {
      const view::Vec3 piece = surfaceMoveSample(path, *startSurf, 0.0f).position;
      const bool blocked =
          st.geometryAlign && startSurf->blocked(from.eye(), piece, 0.02f);
      if (blocked) {
        const view::Vec3 toCam = view::normalize(from.eye() - from.target);
        fresh.alignStart = alignSlideToFace(session->variant(), path.from, toCam,
                                            std::max(1.0f, from.distance));
      } else {
        fresh.alignStart = fresh.startOffset;
      }
    }
  }
  return fresh;
}

void stepShapeSequenceOnce(app::Shell& shell, ShapeMoveSequence& seq, float dt,
                           SurfaceCache* cache) {
  app::Session* session = shell.session();
  app::Settings& st = shell.settings();
  if (session == nullptr) {
    seq.active = false;
    return;
  }
  if (shapeFollowApplies(shell)) {
    // Offered: keep going.
  } else {
    seq.active = false;
    return;
  }

  const view::MovePath& path = session->animation().path();
  const float speed = std::clamp(st.shapeMorphSpeed, 0.25f, 4.0f);
  const float alignSec = kShapeAlignSeconds / speed;
  const float approachSec = kShapeApproachSeconds / speed;
  const float returnSec = kShapeReturnSeconds / speed;

  seq.elapsed += dt;
  const ShapeBeat beat =
      shapeBeat(seq.elapsed, alignSec, approachSec, seq.travelSeconds, returnSec);

  // Pin the piece: still through the lead-in, travelling in the middle, landed through
  // the return.
  const float moveT =
      beat.stage == ShapeStage::Travel                                       ? beat.local
      : (beat.stage == ShapeStage::Done || beat.stage == ShapeStage::Return) ? 1.0f
                                                                             : 0.0f;
  session->setMoveProgress(moveT);

  // The surface at the pose drawn last frame (the offset is written below, so the camera
  // leads the morph by a frame - invisible, and it keeps this to one surface build).
  const std::shared_ptr<const PlaySurface> surf = currentSurface(shell, cache);
  if (surf == nullptr) {
    seq.active = false;
    return;
  }

  // The morph is always the same bounded turn: move the slide toward whichever pose the
  // stage wants, by at most a fixed rate. No stage can spin the board a half-period in a
  // frame, which is what made the shape (and the camera with it) flip.
  const float periodU = 2.0f * static_cast<float>(session->variant().dims.extent(0));
  const float periodV = PlaySurface::slidesAlongRanks(session->variant())
                            ? 2.0f * static_cast<float>(session->variant().dims.extent(1))
                            : 0.0f;
  const float step = kShapeMorphRate * speed * dt;
  const auto morphTo = [&](const SlideOffset& goal) {
    seq.offset = stepSlide(seq.offset, goal, step, step, periodU, periodV);
  };
  const auto sameSlide = [&](float a, float b, float period) {
    if (period <= 0.0f) return std::abs(a - b) < 1e-3f;
    float d = std::fmod(a - b, period);
    if (d < 0.0f) d += period;
    return d < 1e-3f || d > period - 1e-3f;
  };
  bool settled = false;

  // The stages are strictly sequential, and morphing and camera motion do not overlap
  // except inside Travel (where following and morphing go together by definition):
  //   Align   - morph, camera still
  //   Approach- camera to the piece, board still
  //   Travel  - piece moves, camera follows, board morphs as needed
  //   Return  - camera back, board still
  //   Done    - board morphs home, camera still
  switch (beat.stage) {
    case ShapeStage::Align:
      // Morph in place: the camera does not move, only the board turns under it.
      morphTo(seq.alignStart);
      break;
    case ShapeStage::Approach:
      // The camera flies to the piece and the board is held exactly as Align left it.
      break;
    case ShapeStage::Travel: {
      // Re-aim only when the followed cell changes; the search is what says *which* way
      // the board has to turn.
      const CellId cell = currentFollowedCell(shell, *surf);
      if (cell != seq.trackedCell) {
        seq.trackedCell = cell;
        seq.travelTo = alignOffsetFor(shell, cell, cache);
      }
      // Look ahead at where the camera is going. While its own path would cross a square,
      // morph the board toward the clear pose; once the path is clear, hold the pose that
      // cleared it.
      const bool clip = followClips(path, *surf, beat.local, kShapeLookahead,
                                    chaseEyeDistance(*surf), followLift(st));
      if (st.geometryAlign && clip) morphTo(seq.travelTo);
      break;
    }
    case ShapeStage::Return:
      // The camera flies back and the board is held exactly as Travel left it - the shape
      // is turned home only after the camera has arrived (Done).
      break;
    case ShapeStage::Done:
      // Camera home and still: morph the board back to the player's own pose, at the same
      // bounded rate. The stage is time-boxed, but a big offset needs longer: keep
      // turning until it is actually home, so ending the sequence never snaps the shape.
      if (sameSlide(seq.offset.u, seq.startOffset.u, periodU) &&
          sameSlide(seq.offset.v, seq.startOffset.v, periodV)) {
        seq.offset = seq.startOffset;
        settled = true;
      } else {
        morphTo(seq.startOffset);
      }
      break;
  }

  // The followed pose, at the move's own progress: start of travel in Approach, the
  // moving piece in Travel, the landed piece in Return.
  const float followT =
      beat.stage == ShapeStage::Travel                                       ? beat.local
      : (beat.stage == ShapeStage::Done || beat.stage == ShapeStage::Return) ? 1.0f
                                                                             : 0.0f;
  view::OrbitCamera want = session->playerCamera();
  if (st.shapeFollow == "chase") {
    want = surfaceFollowCamera(path, *surf, followT, chaseEyeDistance(*surf),
                               st.followUpright, followLift(st));
  } else {
    // Turntable: the player's own angle is kept - only the look-at follows the piece. The
    // shape is what turns, in the offset below.
    want.target = surfaceMoveSample(path, *surf, followT).position;
  }

  // How much of the piece's own frame the camera takes, per beat. Align leaves the view
  // entirely alone (only the board turns under it); the camera reaches the piece over
  // Approach and holds that frame exactly through Travel - right is `a x b`, up the
  // piece's upright - then gives the player's view back over Return. The ends of every
  // stage are eased, so the camera never starts or stops with a jerk and nothing "jump
  // cuts". `followStrength` is not folded in here: a half-applied intrinsic frame is a
  // half-upright piece, which is the defect this replaces.
  float amount = 1.0f;
  switch (beat.stage) {
    case ShapeStage::Align:
      amount = 0.0f;
      break;
    case ShapeStage::Approach:
      amount = shapeEase(beat.local);
      break;
    case ShapeStage::Return:
      amount = 1.0f - shapeEase(beat.local);
      break;
    case ShapeStage::Done:
      amount = 0.0f;
      break;
    case ShapeStage::Travel:
      amount = 1.0f;
      break;
  }
  // Blend against the player's *live* camera, not the frozen start: an orbit made during
  // the shot is theirs and shows through, and the return has nothing to snap from when it
  // ends (the old flat camera folded its shot in as an offset for the same reason).
  seq.camera = view::slerpCamera(session->playerCamera(), want, amount);

  st.geometryAlignOffset = seq.offset.u;
  st.geometryAlignOffsetV = seq.offset.v;
  if (settled) seq.active = false;
}

ShapeMoveSequence simulateShapeSequence(app::Shell& shell, const view::MovePath& path,
                                        float travelSeconds, float elapsed,
                                        SurfaceCache* cache) {
  app::Settings& st = shell.settings();
  // A fresh sequence starts from the player's own pose, not from wherever a previous
  // simulation left the transient offset, so every call is a pure function of `elapsed`.
  st.geometryAlignOffset = 0.0f;
  st.geometryAlignOffsetV = 0.0f;
  const app::Session* session = shell.session();
  const view::OrbitCamera from =
      session != nullptr ? session->playerCamera() : view::OrbitCamera{};
  ShapeMoveSequence seq = beginShapeSequence(shell, path, travelSeconds, from, cache);
  const int steps =
      elapsed <= 0.0f ? 0 : static_cast<int>(std::floor(elapsed / kShapeSimStep + 1e-4f));
  stepTimes(shell, seq, steps, cache);
  return seq;
}

float shapeSequenceSettleSeconds(app::Shell& shell, const view::MovePath& path,
                                 float travelSeconds, SurfaceCache* cache) {
  app::Settings& st = shell.settings();
  st.geometryAlignOffset = 0.0f;
  st.geometryAlignOffsetV = 0.0f;
  const app::Session* session = shell.session();
  const view::OrbitCamera from =
      session != nullptr ? session->playerCamera() : view::OrbitCamera{};
  ShapeMoveSequence seq = beginShapeSequence(shell, path, travelSeconds, from, cache);
  const int cap = static_cast<int>(kShapeSettleCapSeconds / kShapeSimStep);
  for (int i = 0; i < cap; ++i) {
    stepShapeSequenceOnce(shell, seq, kShapeSimStep, cache);
    if (!seq.active) return static_cast<float>(i + 1) * kShapeSimStep;
  }
  return kShapeSettleCapSeconds;
}

}  // namespace cb::render
