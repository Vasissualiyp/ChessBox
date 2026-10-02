// SPDX-License-Identifier: GPL-3.0-or-later
//
// The playable window.
//
// Deliberately thin. Which screen the player is on, and what happens to a game while
// they are elsewhere, is app::Shell - covered by tests that never open a window. Every
// pixel comes from the renderer and the interface, covered by headless image tests.
// What is left here is translating platform events and deciding when to redraw, which
// is the only part that genuinely needs a display.
#include <SDL3/SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "app/shell.hpp"
#include "io/variant_toml.hpp"
#include "render/board_renderer.hpp"
#include "render/image_io.hpp"
#include "render/window.hpp"
#ifdef CB_HAVE_IMGUI
#include "render/play_surface.hpp"
#include "render/ui.hpp"
#endif

using namespace cb;

namespace {

/// Where the variants live, whether running from the build tree or an install.
std::filesystem::path variantDir() {
  for (const char* base : {"variants", "../variants", "../../variants",
                           "share/chessbox/variants", "/usr/share/chessbox/variants"}) {
    if (std::filesystem::is_directory(base)) return base;
  }
  return "variants";
}

std::vector<std::string> variantLibrary() {
  std::vector<std::string> names;
  std::error_code ec;
  for (const auto& e : std::filesystem::directory_iterator(variantDir(), ec)) {
    if (e.is_regular_file() && e.path().extension() == ".toml") {
      names.push_back(e.path().stem().string());
    }
  }
  std::sort(names.begin(), names.end());
  return names;
}

/// The unshifted character a key produces, for the two-player keyboard; '\0' for a key
/// that is not part of either half. It is the key's identity, so the halves are the same
/// on any layout that has the keys.
char keyChar(SDL_Keycode key) {
  switch (key) {
    case SDLK_Q:
      return 'q';
    case SDLK_W:
      return 'w';
    case SDLK_E:
      return 'e';
    case SDLK_R:
      return 'r';
    case SDLK_T:
      return 't';
    case SDLK_A:
      return 'a';
    case SDLK_S:
      return 's';
    case SDLK_D:
      return 'd';
    case SDLK_F:
      return 'f';
    case SDLK_G:
      return 'g';
    case SDLK_Z:
      return 'z';
    case SDLK_X:
      return 'x';
    case SDLK_C:
      return 'c';
    case SDLK_V:
      return 'v';
    case SDLK_B:
      return 'b';
    case SDLK_Y:
      return 'y';
    case SDLK_U:
      return 'u';
    case SDLK_I:
      return 'i';
    case SDLK_O:
      return 'o';
    case SDLK_P:
      return 'p';
    case SDLK_H:
      return 'h';
    case SDLK_J:
      return 'j';
    case SDLK_K:
      return 'k';
    case SDLK_L:
      return 'l';
    case SDLK_N:
      return 'n';
    case SDLK_M:
      return 'm';
    case SDLK_COMMA:
      return ',';
    case SDLK_PERIOD:
      return '.';
    case SDLK_SEMICOLON:
      return ';';
    case SDLK_APOSTROPHE:
      return '\'';
    case SDLK_SLASH:
      return '/';
    case SDLK_BACKSLASH:
      return '\\';
    case SDLK_TAB:
      return '\t';
    default:
      return '\0';
  }
}

std::unique_ptr<app::Shell> makeShell(const std::filesystem::path& settings = {}) {
  auto shell = app::Shell::create(variantLibrary(), settings);
  shell->setVariantLoader([](const std::string& name) -> Result<VariantSpec> {
    auto v = loadVariantFile(variantDir() / (name + ".toml"));
    if (!v.has_value()) return fail(v.error().code, v.error().message);
    return std::move(*v);
  });
  // The editor reads and writes the author's own file, not the resolved spec.
  shell->setVariantSourceLoader([](const std::string& name) -> Result<std::string> {
    const auto path = variantDir() / (name + ".toml");
    std::ifstream in(path);
    if (!in) {
      return fail(ErrorCode::ParseError,
                  "cannot open variant file '" + path.string() + "'");
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
  });
  shell->setVariantPathResolver(
      [](const std::string& name) { return variantDir() / (name + ".toml"); });
  return shell;
}

render::BoardOptions optionsFrom(const app::Settings& s) {
  render::BoardOptions o;
  o.showLegalMoves = s.showLegalMoves;
  o.showLastMove = s.showLastMove;
  o.showCheck = s.showCheck;
  o.showSeams = s.showSeams;
  o.pieceHeightScale = s.pieceHeightScale;
  o.flat = s.flatView;
  // The player's own slide plus the anti-clip's transient offset. Both the renderer and
  // `poseFrom` must fold the offset in: the camera was targeting the aligned surface
  // while the board was still drawn unaligned, so the align affected the camera but the
  // shape never visibly turned (M17.17).
  o.surfaceSlideU = s.geometrySlideU + s.geometryAlignOffset;
  o.surfaceSlideV = s.geometrySlideV + s.geometryAlignOffsetV;
  o.surfaceEvert = s.geometryEvert;
  o.surfaceGhost = s.geometryGhost;
  o.surfaceTwist = s.kleinTwist;
  o.surfaceOpenness = s.geometryWidth;
  return o;
}

/// The board options for the shell's current variant: the geometry view is offered only
/// where there is a surface to become, so `standard` and every non-glued board stay flat.
render::BoardOptions optionsFor(const app::Shell& shell) {
  render::BoardOptions o = optionsFrom(shell.settings());
#ifdef CB_HAVE_IMGUI
  const app::Session* session = shell.session();
  o.surface = shell.settings().geometryView && session != nullptr &&
              render::hasPlaySurface(session->variant());
#else
  (void)shell;
#endif
  return o;
}

#ifdef CB_HAVE_IMGUI
/// How the geometry view's surface is posed right now. The variant argument is kept so
/// the call sites read the same as before; nothing in the pose depends on it now that the
/// Klein twist is a plain half-turn count.
render::SurfacePose poseFrom(const app::Settings& s, const VariantSpec&) {
  render::SurfacePose pose;
  pose.slideU = s.geometrySlideU + s.geometryAlignOffset;
  pose.slideV = s.geometrySlideV + s.geometryAlignOffsetV;
  pose.evert = s.geometryEvert;
  pose.twist = s.kleinTwist;
  pose.openness = s.geometryWidth;
  return pose;
}

namespace {

float wrapAngle(float a) {
  constexpr float kPi = 3.14159265358979f;
  constexpr float kTwoPi = 2.0f * kPi;
  while (a > kPi) a -= kTwoPi;
  while (a < -kPi) a += kTwoPi;
  return a;
}

/// Blend the player's own camera towards a follow pose by `k` in [0,1], the same way
/// `Session::cameraOver` folds in the flat move camera: yaw and roll wrap, pitch stays
/// off its poles, and the target and distance interpolate. The follow then eases in and
/// out with `view::shotEnvelope`, so a move neither snaps onto the piece at the first
/// frame nor jerks back at the last.
view::OrbitCamera blendCamera(const view::OrbitCamera& base,
                              const view::OrbitCamera& want, float k) {
  view::OrbitCamera out = base;
  out.target = base.target + (want.target - base.target) * k;
  out.yaw = base.yaw + wrapAngle(want.yaw - base.yaw) * k;
  out.pitch = std::clamp(base.pitch + (want.pitch - base.pitch) * k, 0.10f, 1.53f);
  out.distance = std::max(1.0f, base.distance + (want.distance - base.distance) * k);
  out.roll = base.roll + wrapAngle(want.roll - base.roll) * k;
  return out;
}

/// How far the chase camera sits from the followed piece: close enough to make the piece
/// the subject, at a fraction of the shape's span. The anti-clip search tests occlusion
/// at this same distance, so a seat it finds clear is clear for the camera that follows.
float chaseEyeDistance(const render::PlaySurface& surf) {
  const view::Bounds b = surf.bounds();
  const float span = std::max({b.maxX - b.minX, b.maxY - b.minY, b.maxZ - b.minZ});
  return std::max(2.0f, 0.7f * span);
}

/// The chase camera's elevation as the tangent the shader/camera maths wants: the
/// settings slider is in degrees because that is what a player reads.
float followLift(const app::Settings& s) {
  constexpr float kPi = 3.14159265358979f;
  return std::tan(s.followElevationDeg * kPi / 180.0f);
}

}  // namespace

/// The four-beat choreography for a followed move on a shape (M17.19). On a move the
/// board
/// **morphs** (its slide) until the player's own line to the start square is clear, the
/// camera flies to the piece, the piece travels with the camera chasing it, and the
/// camera flies home while the board morphs back. The piece is held still until the
/// camera has reached it, so nothing moves before you are looking at it. Pure beats in
/// `render::shapeBeat`; this holds only the clock and the values that must persist
/// between frames. Inactive means "no choreography": the old follow behaviour (a
/// capture's, too).
struct ShapeMoveSequence {
  bool active{false};
  float elapsed{0.0f};
  float travelSeconds{1.0f};
  CellId tokenFrom{kInvalidCell};
  CellId tokenTo{kInvalidCell};
  view::OrbitCamera startCam{};  ///< the player's own camera when the move began
  view::OrbitCamera camera{};    ///< the effective camera for the frame just computed
  render::SlideOffset startOffset{};  ///< the transient offset when the move began
  render::SlideOffset alignStart{};   ///< Align's target: clear from the start camera
  render::SlideOffset travelTo{};     ///< Travel's target: clear along the route
  render::SlideOffset offset{};       ///< the live transient offset, written to settings
  CellId trackedCell{kInvalidCell};
};

/// The choreography to hand a path that must not run it: an inactive sequence falls back
/// to the steady-state follow, which is what every capture wants.
const ShapeMoveSequence kNoShapeSequence{};

/// The seats as a placement list, so the move camera can be asked for its blend against
/// the shape instead of the flat layout (M17.16).
std::vector<view::Placement> surfacePlacements(const render::PlaySurface& surf) {
  std::vector<view::Placement> out;
  out.reserve(surf.seats().size());
  for (const render::SurfaceSeat& s : surf.seats()) {
    out.push_back({s.cell, s.centre.x, s.centre.y, s.centre.z, 0});
  }
  return out;
}

/// The effective camera for what is drawn right now: the move-camera blend against the
/// shape while the geometry view is on, the session's own camera otherwise. One function
/// so the render and the pick cannot ask for different cameras (ADR-0011).
///
/// While a shape-follow choreography is running (`seq.active`) that is the camera,
/// because it is already a blend of the player's own view and the follow (M17.19). The
/// rest of this is the capture path's steady-state follow - a `--clip` of a move never
/// runs the choreography, and its frames must stay byte-for-byte what they were.
view::OrbitCamera boardCamera(const app::Shell& shell, const ShapeMoveSequence& seq) {
  const app::Session* session = shell.session();
  if (session == nullptr) return {};
  const app::Settings& st = shell.settings();
  if (optionsFor(shell).surface) {
    if (seq.active) return seq.camera;
    const render::PlaySurface surf =
        render::PlaySurface::build(session->variant(), poseFrom(st, session->variant()));
    if (!surf.empty()) {
      const view::OrbitCamera settled = session->playerCamera();
      if (session->shotInFlight()) {
        const view::MovePath& path = session->animation().path();
        const float t = session->animation().progress();
        view::OrbitCamera want = settled;
        if (st.shapeFollow == "chase") {
          // A third-person chase: the piece centred, the camera behind it along its
          // route and standing it upright on its normal (M17.16), close enough that the
          // piece is the subject. The anti-clip turns the board so this does not put the
          // camera inside the tube.
          want = render::surfaceFollowCamera(path, surf, t, chaseEyeDistance(surf),
                                             st.followUpright, followLift(st));
        } else {
          // Turntable: the camera angle is the player's; only the look-at follows the
          // piece. The anti-clip turns the *board* so nothing comes between them
          // (M17.16/17).
          want.target = render::surfaceMoveSample(path, surf, t).position;
        }
        // Ease in and out at the ends, and respect the follow-strength setting, so the
        // shape follow settles like the flat board's does rather than snapping.
        return blendCamera(settled, want,
                           session->followStrength() * view::shotEnvelope(t));
      }
      return settled;
    }
  }
  return session->camera();
}

/// The seat the followed move is on right now, by nearest seat to the sampled position.
/// Aligning for the *current* cell rather than the destination keeps a long move clear
/// of the shape the whole way across, not only at the square it lands on.
CellId currentFollowedCell(const app::Shell& shell, const render::PlaySurface& surf) {
  const app::Session* session = shell.session();
  if (session == nullptr || !session->shotInFlight()) return kInvalidCell;
  const view::Vec3 here = render::surfaceMoveSample(session->animation().path(), surf,
                                                    session->animation().progress())
                              .position;
  CellId best = kInvalidCell;
  float bestDist = 1e30f;
  for (const render::SurfaceSeat& s : surf.seats()) {
    const float d = view::length(s.centre - here);
    if (d < bestDist) {
      bestDist = d;
      best = s.cell;
    }
  }
  return best;
}

/// The slide offsets that keep the cell `followed` from clipping, for the current
/// settings and camera. Pure and deterministic, so a capture gets the same value the
/// interactive loop eases towards - without it a `--clip` of a followed move would show
/// the camera looking through the shape (M17.17).
render::SlideOffset alignOffsetFor(const app::Shell& shell, CellId followed) {
  const app::Session* session = shell.session();
  const app::Settings& st = shell.settings();
  if (session == nullptr || followed == kInvalidCell || !st.geometryAlign ||
      !session->shotInFlight()) {
    return {};
  }
  if (st.shapeFollow == "chase") {
    const render::PlaySurface surf =
        render::PlaySurface::build(session->variant(), poseFrom(st, session->variant()));
    const view::MovePath& path = session->animation().path();
    const float t = session->animation().progress();
    const view::Vec3 here = render::surfaceMoveSample(path, surf, t).position;
    const view::Vec3 ahead =
        render::surfaceMoveSample(path, surf, std::min(1.0f, t + 0.05f)).position;
    return render::alignSlideU(session->variant(), followed, ahead - here,
                               chaseEyeDistance(surf), followLift(st));
  }
  const view::OrbitCamera base = session->playerCamera();
  const view::Vec3 toCamera = view::normalize(base.eye() - base.target);
  return render::alignSlideToFace(session->variant(), followed, toCamera, base.distance);
}

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

render::SlideOffset stepSlide(const render::SlideOffset& cur,
                              const render::SlideOffset& goal, float maxStepU,
                              float maxStepV, float periodU, float periodV) {
  return {stepSlideCoord(cur.u, goal.u, maxStepU, periodU),
          stepSlideCoord(cur.v, goal.v, maxStepV, periodV)};
}

}  // namespace

/// Advance the shape-follow choreography one frame (M17.19). It leaves the effective
/// camera in `seq.camera` and the transient slide in the settings. The piece's own clock
/// is driven here too: pinned at the start through the lead-in, running during Travel,
/// pinned at the end through the return - so nothing moves before the camera has reached
/// it, and nothing moves again once it has landed.
void updateShapeSequence(app::Shell& shell, ShapeMoveSequence& seq, float dt) {
  app::Session* session = shell.session();
  app::Settings& st = shell.settings();
  if (session == nullptr || !shell.showsBoard() || !optionsFor(shell).surface ||
      !render::hasPlaySurface(session->variant())) {
    seq.active = false;
    return;
  }
  if (st.cameraMode == "off" || session->followStrength() <= 0.0f) {
    seq.active = false;
    return;
  }

  const view::MovePath& path = session->animation().path();
  const bool animating = session->animation().active();

  if (animating &&
      (!seq.active || seq.tokenFrom != path.from || seq.tokenTo != path.to)) {
    // A new move. The camera to start from is the last one actually drawn, so a move that
    // begins while an earlier return is still running has no cut.
    const view::OrbitCamera from = seq.active ? seq.camera : session->playerCamera();
    ShapeMoveSequence fresh;
    fresh.active = true;
    fresh.travelSeconds = std::max(1e-3f, session->animation().duration());
    fresh.tokenFrom = path.from;
    fresh.tokenTo = path.to;
    fresh.startCam = from;
    fresh.camera = from;
    fresh.startOffset = {st.geometryAlignOffset, st.geometryAlignOffsetV};
    fresh.offset = fresh.startOffset;
    // Morph at the start only if the player's own line to the piece is actually blocked
    // by a square - the morph exists to clear a blocked view, not to turn a clear one.
    {
      const render::PlaySurface startSurf = render::PlaySurface::build(
          session->variant(), poseFrom(st, session->variant()));
      const view::Vec3 piece = render::surfaceMoveSample(path, startSurf, 0.0f).position;
      const bool blocked =
          st.geometryAlign && startSurf.blocked(from.eye(), piece, 0.02f);
      if (blocked) {
        const view::Vec3 toCam = view::normalize(from.eye() - from.target);
        fresh.alignStart = render::alignSlideToFace(session->variant(), path.from, toCam,
                                                    std::max(1.0f, from.distance));
      } else {
        fresh.alignStart = fresh.startOffset;
      }
    }
    seq = fresh;
    session->setMoveProgress(0.0f);
  }
  if (!seq.active) return;

  const float speed = std::clamp(st.shapeMorphSpeed, 0.25f, 4.0f);
  const float alignSec = kShapeAlignSeconds / speed;
  const float approachSec = kShapeApproachSeconds / speed;
  const float returnSec = kShapeReturnSeconds / speed;

  seq.elapsed += dt;
  const render::ShapeBeat beat =
      render::shapeBeat(seq.elapsed, alignSec, approachSec, seq.travelSeconds, returnSec);

  // Pin the piece: still through the lead-in, travelling in the middle, landed through
  // the return.
  const float moveT = beat.stage == render::ShapeStage::Travel ? beat.local
                      : (beat.stage == render::ShapeStage::Done ||
                         beat.stage == render::ShapeStage::Return)
                          ? 1.0f
                          : 0.0f;
  session->setMoveProgress(moveT);

  // The surface at the pose drawn last frame (the offset is written below, so the camera
  // leads the morph by a frame - invisible, and it keeps this to one surface build).
  const render::PlaySurface surf =
      render::PlaySurface::build(session->variant(), poseFrom(st, session->variant()));

  // The morph is always the same bounded turn: move the slide toward whichever pose the
  // stage wants, by at most a fixed rate. No stage can spin the board a half-period in a
  // frame, which is what made the shape (and the camera with it) flip.
  const float periodU = 2.0f * static_cast<float>(session->variant().dims.extent(0));
  const float periodV = render::PlaySurface::slidesAlongRanks(session->variant())
                            ? 2.0f * static_cast<float>(session->variant().dims.extent(1))
                            : 0.0f;
  const float step = kShapeMorphRate * speed * dt;
  const auto morphTo = [&](const render::SlideOffset& goal) {
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
    case render::ShapeStage::Align:
      // Morph in place: the camera does not move, only the board turns under it.
      morphTo(seq.alignStart);
      break;
    case render::ShapeStage::Approach:
      // The camera flies to the piece and the board is held exactly as Align left it.
      break;
    case render::ShapeStage::Travel: {
      // Re-aim only when the followed cell changes; the search is what says *which* way
      // the board has to turn.
      const CellId cell = currentFollowedCell(shell, surf);
      if (cell != seq.trackedCell) {
        seq.trackedCell = cell;
        seq.travelTo = alignOffsetFor(shell, cell);
      }
      // Look ahead at where the camera is going. While its own path would cross a square,
      // morph the board toward the clear pose; once the path is clear, hold the pose that
      // cleared it.
      const bool clip = render::followClips(path, surf, beat.local, kShapeLookahead,
                                            chaseEyeDistance(surf), followLift(st));
      if (st.geometryAlign && clip) morphTo(seq.travelTo);
      break;
    }
    case render::ShapeStage::Return:
      // The camera flies back and the board is held exactly as Travel left it - the shape
      // is turned home only after the camera has arrived (Done).
      break;
    case render::ShapeStage::Done:
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
  const float followT = beat.stage == render::ShapeStage::Travel ? beat.local
                        : (beat.stage == render::ShapeStage::Done ||
                           beat.stage == render::ShapeStage::Return)
                            ? 1.0f
                            : 0.0f;
  view::OrbitCamera want = session->playerCamera();
  if (st.shapeFollow == "chase") {
    want = render::surfaceFollowCamera(path, surf, followT, chaseEyeDistance(surf),
                                       st.followUpright, followLift(st));
  } else {
    // Turntable: the player's own angle is kept - only the look-at follows the piece. The
    // shape is what turns, in the offset below.
    want.target = render::surfaceMoveSample(path, surf, followT).position;
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
    case render::ShapeStage::Align:
      amount = 0.0f;
      break;
    case render::ShapeStage::Approach:
      amount = render::shapeEase(beat.local);
      break;
    case render::ShapeStage::Return:
      amount = 1.0f - render::shapeEase(beat.local);
      break;
    case render::ShapeStage::Done:
      amount = 0.0f;
      break;
    case render::ShapeStage::Travel:
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

/// Put the camera round whatever the board has just become.
///
/// The shape and the flat board are different sizes and sit in different places, so a
/// toggle that left the camera where it was left the board off in a corner of the
/// window. The angle the player was looking from is kept: only the frame moves.
void frameBoard(app::Shell& shell) {
  app::Session* session = shell.session();
  if (session == nullptr) return;
  render::frameGeometryCamera(*session, optionsFor(shell),
                              poseFrom(shell.settings(), session->variant()));
}
#endif

/// Tell the renderer what the engine says just happened, so the board can show it.
void syncMarks(render::BoardRenderer& renderer, const app::Session& session) {
  const auto& history = session.game().moveHistory();
  if (history.empty()) {
    renderer.setLastMove(kInvalidCell, kInvalidCell);
  } else {
    renderer.setLastMove(history.back().from, history.back().to);
  }
  renderer.setCheckCell(
      session.game().inCheck()
          ? session.game().position().findRoyal(session.game().position().sideToMove())
          : kInvalidCell);
}

/// Render a frame - board and interface together - to a PPM, and exit.
///
/// It needs no display: SDL's dummy video driver supplies the window the interface uses
/// for input mapping, and the frame goes to the same offscreen target the headless tests
/// use. That makes the whole screen capturable on a machine with no compositor, which is
/// how the interface gets reviewed at all.
///
/// `clip` turns one frame into a sequence: `t` is swept from `clipT0` to `clipT1` over
/// `clipFrames` frames, each rendered at that fixed `t` and written as
/// `<dir>/frame_%04d.ppm`. That is the deterministic, dependency-light half of the clip
/// export (M12.2): a directory of numbered frames is the guaranteed output, and muxing a
/// video from them is an external convenience, never a codec in the engine.
int captureFrame(const std::string& variantName, const std::string& path,
                 const std::string& script, const std::string& screen, float overtureT,
                 int previewDims, bool clip, int clipFrames, float clipT0, float clipT1,
                 bool cinema, const std::string& followMode,
                 const std::string& shapeFollow, float moveT, int benchFrames,
                 bool geometry, float evert, float slideU, float slideV, float ghost) {
  // Captures use default settings, never the person's own. A screenshot that changes
  // because whoever ran it likes a larger interface is not a screenshot of the game -
  // and `ctest -R gui-` would then pass or fail by whose machine it ran on.
  auto shell = makeShell(std::filesystem::temp_directory_path() /
                         "chessbox-capture-defaults.conf");
  if (auto ok = shell->startGame(variantName); !ok.has_value()) {
    std::fprintf(stderr, "cannot load '%s': %s\n", variantName.c_str(),
                 ok.error().format().c_str());
    return 1;
  }
  if (!script.empty()) {
    if (auto ok = shell->session()->applyScript(script); !ok.has_value()) {
      std::fprintf(stderr, "script: %s\n", ok.error().format().c_str());
      return 1;
    }
  }
  // A capture can state that it wants the play board as its own shape (M17), so the
  // surface is reviewable the same way every other screen is.
  if (geometry) shell->settings().geometryView = true;
  shell->settings().geometryEvert = evert;
  shell->settings().geometrySlideU = slideU;
  shell->settings().geometrySlideV = slideV;
  shell->settings().geometryGhost = ghost;
#ifdef CB_HAVE_IMGUI
  frameBoard(*shell);
#endif
  if (screen == "menu")
    shell->go(app::Screen::MainMenu);
  else if (screen == "welcome")
    shell->go(app::Screen::Welcome);
  else if (screen == "pause")
    shell->pause();
  else if (screen == "settings")
    shell->go(app::Screen::Settings);
  else if (screen == "editor")
    shell->go(app::Screen::Editor);
  else if (screen == "designer" || screen == "body" || screen == "flat") {
    // The designers need a document open, which the editor's own entry does for them.
    (void)shell->openEditor(variantName);
    shell->go(app::Screen::Editor);
  } else if (screen == "info")
    shell->go(app::Screen::GameInfo);
  else if (screen == "pieces")
    shell->go(app::Screen::PieceMoves);
  else if (screen == "newgame") {
    shell->go(app::Screen::NewGame);
    // The named variant is the one under review, so the capture shows *its* overture
    // rather than whatever the picker would have opened on.
    shell->overtures().jumpTo(app::overtureFor(variantName));
    // A canonical point in the overture's cycle, stated rather than integrated towards:
    // the capture feeds a fixed timestep on purpose, and a screenshot of the library has
    // to be the same picture every time it is taken. Late enough that the selected
    // variant's shape has formed and there is something to look at.
    shell->overtures().setProgress(overtureT);
  } else if (screen == "quit")
    shell->go(app::Screen::QuitConfirm);

  // A capture that wants to show the move camera states it rather than relying on the
  // player's settings (a capture uses defaults, where following is off). `--move-t` pins
  // the move itself, the same way `--t` pins the overture.
  if (shell->session() != nullptr) {
    if (!followMode.empty()) {
      shell->session()->setCameraMode(followMode);
      shell->session()->setFollowStrength(1.0f);
    }
    if (!shapeFollow.empty()) shell->settings().shapeFollow = shapeFollow;
    if (moveT >= 0.0f) shell->session()->setMoveProgress(moveT);
  }

  if (SDL_getenv("DISPLAY") == nullptr && SDL_getenv("WAYLAND_DISPLAY") == nullptr) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
  }
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("ChessBox", 1440, 900, SDL_WINDOW_HIDDEN);
  if (window == nullptr) {
    std::fprintf(stderr, "cannot create a hidden window: %s\n", SDL_GetError());
    return 1;
  }

  auto ctx = render::VulkanContext::create();
  if (!ctx.has_value()) {
    std::fprintf(stderr, "%s\n", ctx.error().format().c_str());
    return 1;
  }
  auto renderer = render::BoardRenderer::create(*ctx);
  auto target = render::OffscreenTarget::create(*ctx, 1440, 900);
  if (!renderer.has_value() || !target.has_value()) {
    std::fprintf(stderr, "cannot set up the renderer\n");
    return 1;
  }
  renderer->setOptions(optionsFor(*shell));

#ifdef CB_HAVE_IMGUI
  auto ui = render::Ui::create(*ctx, window, renderer->theme());
  if (!ui.has_value()) {
    std::fprintf(stderr, "%s\n", ui.error().format().c_str());
    return 1;
  }
  // The capture has to show the interface at the scale the player set, or it is not a
  // picture of what they would see.
  (void)(*ui)->setScale(shell->settings().guiScale);
  (*ui)->setIconStyle(render::iconStyleFromName(shell->settings().pieceIcons));
  {
    const view::Theme theme = view::themeFromName(shell->settings().theme);
    renderer->setTheme(theme);
    (*ui)->setTheme(theme);
  }
  // Which page of the editor a designer capture wants. Set here rather than inside the
  // loop: it is where the review is pointed, not something a frame decides.
  if (screen == "designer") (*ui)->openEditorPage(1, 0);
  if (screen == "body") (*ui)->openEditorPage(1, 1);
  if (screen == "flat") (*ui)->openEditorPage(1, 2);
  (*ui)->setPreviewDims(previewDims);
  // Point the library at the named variant, so a capture of a variant with a derived
  // overture shows *it* rather than the standard construction the first entry opens on.
  if (screen == "newgame") (*ui)->pickVariant(variantName);
  // One frame, built and rendered at whatever `t` the caller has pinned. Split out from
  // writing so `--clip` can call it once per frame. `dt` is the interface's own clock:
  // the warm-up gives it enough to settle a screen transition, and the real frames give
  // it none so the drifting field is the same in every frame of a clip.
  const auto renderFrame = [&](float dt) -> std::optional<render::Image> {
    // A fixed step rather than a real clock: the shell animates, and a capture has to be
    // the same picture every time it is taken.
    (*ui)->tick(dt);
    (*ui)->newFrame();
    const render::UiRequest request = (*ui)->build(*shell, 60.0f);
    (*ui)->endFrame();
    const render::BoardRect uiRect{request.boardRect[0], request.boardRect[1],
                                   request.boardRect[2], request.boardRect[3]};
    // Cinema is the board and the move and nothing else: the whole frame is the board and
    // the interface emits nothing (the overlay is not recorded), so a clip reads as
    // footage rather than as a screenshot of the game's furniture.
    const render::BoardRect frame =
        cinema ? render::BoardRect{0.0f, 0.0f, static_cast<float>(target->width()),
                                   static_cast<float>(target->height())}
               : uiRect;
    render::InstanceSet instances;
    // The geometry view builds the warped board as ordinary instances, so the same
    // render path draws it (M17); only the surface flag on the options selects it.
    if (shell->showsBoard()) {
      // A capture of a screen that sits over the board shows it exactly as a player
      // would see it - stepped back and out of focus - which is also what puts the blur
      // passes under the validation layers in `ctest -R gui-pause`. Cinema never blurs:
      // the board is the subject.
      const float away = (shell->screen() == app::Screen::Game || cinema) ? 0.0f : 1.0f;
      shell->session()->setPullBack(away);
      renderer->setBlur(away);
      if (frame.valid()) shell->session()->setBoardAspect(frame.width / frame.height);
      // The interactive loop eases this; a capture states it outright, so a followed
      // move is anti-clipped in a clip too (M17.17).
      if (optionsFor(*shell).surface) {
        const render::PlaySurface alignSurf = render::PlaySurface::build(
            shell->session()->variant(),
            poseFrom(shell->settings(), shell->session()->variant()));
        const render::SlideOffset off =
            alignOffsetFor(*shell, currentFollowedCell(*shell, alignSurf));
        shell->settings().geometryAlignOffset = off.u;
        shell->settings().geometryAlignOffsetV = off.v;
        // The offsets just changed, so the renderer's options must be refreshed before
        // the board is built from them.
        renderer->setOptions(optionsFor(*shell));
      }
      syncMarks(*renderer, *shell->session());
      instances = renderer->buildInstances(
          shell->session()->snapshot(), shell->session()->viewConfig(),
          &shell->session()->seams(), &shell->session()->animation(),
          [&](CellId c) { return shell->session()->boardVisible(c); },
          [&](CellId c) { return shell->session()->game().cellInPresent(c); },
          shell->session()->timelineLinks());
    }
    const view::OrbitCamera camera =
        shell->showsBoard() ? boardCamera(*shell, kNoShapeSequence) : view::OrbitCamera{};
    const std::function<void(VkCommandBuffer)> drawUi = [&](VkCommandBuffer cmd) {
      (*ui)->record(cmd);
    };
    const std::function<void(VkCommandBuffer)> noUi{};
    if (auto ok =
            renderer->render(*target, instances, camera, cinema ? noUi : drawUi, frame);
        !ok.has_value()) {
      std::fprintf(stderr, "%s\n", ok.error().format().c_str());
      return std::nullopt;
    }
    auto pixels = target->readPixels();
    if (!pixels.has_value()) {
      std::fprintf(stderr, "%s\n", pixels.error().format().c_str());
      return std::nullopt;
    }
    render::Image img;
    img.width = target->width();
    img.height = target->height();
    img.rgba = std::move(*pixels);
    return img;
  };
  const auto writeFrame = [](const render::Image& img, const std::string& outPath) {
    if (auto ok = render::writePpm(img, outPath); !ok.has_value()) {
      std::fprintf(stderr, "%s\n", ok.error().format().c_str());
      return false;
    }
    std::printf("wrote %s\n", outPath.c_str());
    return true;
  };

  // Two thrown-away frames, in this order: the first lets a screen-change reset the pane
  // transition (which it does after the tick), the second advances that transition to
  // done - without both, the library draws at alpha zero. Then ImGui has a previous frame
  // to size from as well.
  if (!renderFrame(0.0f).has_value()) return 1;
  if (!renderFrame(1.0f).has_value()) return 1;

  if (benchFrames > 0) {
    // A frame-time benchmark with no display: the same offscreen path `--shot` uses, run
    // N times. It measures the CPU recording plus the GPU wait, which is the per-frame
    // cost a player pays - a number for a regression, not an opinion (M4.8).
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < benchFrames; ++i) {
      if (!renderFrame(0.0f).has_value()) return 1;
    }
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0)
            .count();
    const double ms = us / 1000.0 / static_cast<double>(benchFrames);
    std::printf("frame time: %.3f ms  (%.1f fps)  over %d frames\n", ms, 1000.0 / ms,
                benchFrames);
    return 0;
  }

  if (!clip) {
    const std::optional<render::Image> img = renderFrame(0.0f);
    if (!img.has_value() || !writeFrame(*img, path)) return 1;
  } else {
    // A directory of numbered frames: the guaranteed output. Muxing a video from them is
    // an external step, so no codec enters the build.
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    const int frames = std::max(1, clipFrames);
    for (int i = 0; i < frames; ++i) {
      const float t = frames <= 1 ? clipT0
                                  : clipT0 + (clipT1 - clipT0) * static_cast<float>(i) /
                                                 static_cast<float>(frames - 1);
      // Pin the cycle outright: a clip is only deterministic if every frame states its
      // own `t` rather than integrating towards it. The library sweeps the overture;
      // every other screen sweeps the running move, so a board clip is a gameplay shot.
      if (screen == "newgame") {
        shell->overtures().setProgress(t);
      } else if (shell->session() != nullptr) {
        shell->session()->setMoveProgress(t);
      }
      char name[32];
      std::snprintf(name, sizeof(name), "frame_%04d.ppm", i);
      const std::optional<render::Image> img = renderFrame(0.0f);
      if (!img.has_value() ||
          !writeFrame(*img, (std::filesystem::path(path) / name).string())) {
        return 1;
      }
    }
  }
#else
  const auto instances = renderer->buildInstances(
      shell->session()->snapshot(), shell->session()->viewConfig(),
      &shell->session()->seams(), &shell->session()->animation(),
      [&](CellId c) { return shell->session()->boardVisible(c); });
  (void)renderer->render(*target, instances, shell->session()->camera());
  auto pixels = target->readPixels();
  if (!pixels.has_value()) {
    std::fprintf(stderr, "%s\n", pixels.error().format().c_str());
    return 1;
  }
  render::Image img;
  img.width = target->width();
  img.height = target->height();
  img.rgba = std::move(*pixels);
  if (auto ok = render::writePpm(img, path); !ok.has_value()) {
    std::fprintf(stderr, "%s\n", ok.error().format().c_str());
    return 1;
  }
#endif

  const std::size_t validation = ctx->validationErrorCount();
  for (const std::string& message : ctx->takeValidationMessages()) {
    std::fprintf(stderr, "%s\n", message.c_str());
  }
  std::printf("wrote %s\n", path.c_str());
  return validation == 0 ? 0 : 2;
}

}  // namespace

int main(int argc, char** argv) {
  std::string variantName;
  std::string shotPath;
  std::string script;
  std::string screen;
  std::string clipDir;
  std::string followMode;
  std::string shapeFollow;
  float overtureT = 0.78f;
  float moveT = -1.0f;
  int previewDims = 2;
  int clipFrames = 96;
  float clipT0 = 0.0f;
  float clipT1 = 1.0f;
  bool cinema = false;
  int benchFrames = 0;
  bool geometry = false;
  float evert = 0.0f;
  float slideU = 0.0f;
  float slideV = 0.0f;
  float ghost = 1.0f;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      std::printf(
          "chessbox_gui [variant] [--shot FILE [--screen NAME]] [--script TEXT]\n\n"
          "  left click    select a piece, then a lit cell to move it\n"
          "  right drag    orbit     wheel  zoom\n"
          "  g             the board as its own shape; middle drag slides it round\n"
          "  Esc           pause     u  undo     r  reset\n\n"
          "With no variant, the game opens on the main menu.\n"
          "--shot renders one frame to a PPM and exits, with no display required;\n"
          "--screen picks which one: menu, welcome, newgame, pause, settings, editor,\n"
          "designer, body, flat, info, pieces, or the board by default.\n"
          "--t 0..1 is where the library screen's overture is in its cycle: 0 the flat\n"
          "board every variant starts from, 1 the shape it becomes.\n"
          "--clip DIR renders a sequence of numbered PPM frames instead of one shot,\n"
          "sweeping t from --t0 to --t1 over --frames frames (default 0..1, 96).\n"
          "--cinema drops the interface and fills the frame with the board.\n"
          "--follow off|piece|route and --move-t 0..1 show the move camera in a "
          "capture;\n"
          "--shape-follow turntable|chase picks whether the board turns under a fixed\n"
          "camera or the camera chases the piece.\n"
          "--bench-frame N times N headless frames of the chosen screen and prints "
          "ms/frame.\n"
          "--geometry draws the play board as its own shape (a cylinder, a torus, ...),\n"
          "and --evert 0..1 turns that shape through itself - a torus inside out,\n"
          "while --slide and --slide-v move the board round the shape, in cells, and\n"
          "--ghost 0.35..1 makes it translucent so the far side shows through.\n"
          "--dims 2..4 is how many dimensions the designer's move preview shows.\n");
      return 0;
    }
    if (arg == "--shot" && i + 1 < argc)
      shotPath = argv[++i];
    else if (arg == "--clip" && i + 1 < argc)
      clipDir = argv[++i];
    else if (arg == "--script" && i + 1 < argc)
      script = argv[++i];
    else if (arg == "--screen" && i + 1 < argc)
      screen = argv[++i];
    else if (arg == "--t" && i + 1 < argc)
      overtureT = std::strtof(argv[++i], nullptr);
    else if (arg == "--t0" && i + 1 < argc)
      clipT0 = std::strtof(argv[++i], nullptr);
    else if (arg == "--t1" && i + 1 < argc)
      clipT1 = std::strtof(argv[++i], nullptr);
    else if (arg == "--frames" && i + 1 < argc)
      clipFrames = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (arg == "--cinema")
      cinema = true;
    else if (arg == "--geometry")
      geometry = true;
    else if (arg == "--evert" && i + 1 < argc)
      evert = std::strtof(argv[++i], nullptr);
    else if (arg == "--slide" && i + 1 < argc)
      slideU = std::strtof(argv[++i], nullptr);
    else if (arg == "--slide-v" && i + 1 < argc)
      slideV = std::strtof(argv[++i], nullptr);
    else if (arg == "--ghost" && i + 1 < argc)
      ghost = std::strtof(argv[++i], nullptr);
    else if (arg == "--follow" && i + 1 < argc)
      followMode = argv[++i];
    else if (arg == "--shape-follow" && i + 1 < argc)
      shapeFollow = argv[++i];
    else if (arg == "--move-t" && i + 1 < argc)
      moveT = std::strtof(argv[++i], nullptr);
    else if (arg == "--bench-frame" && i + 1 < argc)
      benchFrames = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (arg == "--dims" && i + 1 < argc)
      previewDims = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    else if (!arg.starts_with("-"))
      variantName = arg;
  }
  const std::string& targetPath = !clipDir.empty() ? clipDir : shotPath;
  if (!targetPath.empty() || benchFrames > 0) {
    return captureFrame(variantName.empty() ? "standard" : variantName, targetPath,
                        script, screen, overtureT, previewDims, !clipDir.empty(),
                        clipFrames, clipT0, clipT1, cinema, followMode, shapeFollow,
                        moveT, benchFrames, geometry, evert, slideU, slideV, ghost);
  }

  auto shell = makeShell();
  // Naming a variant on the command line starts it directly; otherwise the game opens
  // where a game should, on its menu.
  if (!variantName.empty()) {
    if (auto ok = shell->startGame(variantName); !ok.has_value()) {
      std::fprintf(stderr, "cannot load '%s': %s\n", variantName.c_str(),
                   ok.error().format().c_str());
      return 1;
    }
  }

  auto window = render::Window::create("ChessBox", 1440, 900);
  if (!window.has_value()) {
    std::fprintf(stderr, "cannot open a window: %s\n", window.error().format().c_str());
    return 1;
  }
  auto renderer = render::BoardRenderer::create(window->context(), window->colorFormat());
  if (!renderer.has_value()) {
    std::fprintf(stderr, "cannot create the renderer: %s\n",
                 renderer.error().format().c_str());
    return 1;
  }
  renderer->setOptions(optionsFor(*shell));

#ifdef CB_HAVE_IMGUI
  auto ui = render::Ui::create(window->context(), window->handle(), renderer->theme(),
                               window->colorFormat());
  if (!ui.has_value()) {
    std::fprintf(stderr, "cannot create the interface: %s\n",
                 ui.error().format().c_str());
    return 1;
  }
  (void)(*ui)->setScale(shell->settings().guiScale);
  (*ui)->setIconStyle(render::iconStyleFromName(shell->settings().pieceIcons));
  {
    const view::Theme theme = view::themeFromName(shell->settings().theme);
    renderer->setTheme(theme);
    (*ui)->setTheme(theme);
  }
#endif
  (void)window->setVsync(shell->settings().vsync);
  if (shell->settings().fullscreen) SDL_SetWindowFullscreen(window->handle(), true);

  render::BoardRect boardRect{0, 0, static_cast<float>(window->width()),
                              static_cast<float>(window->height())};
  // How far the camera has stepped back off the board. Pause is not a dialog landing on
  // top of the position - it is the player looking up from it - so the view pulls away
  // rather than being covered. It lives on the session so that the board, the flat
  // board's pieces and the picking ray all see the same camera.
  float steppedBack = 0.0f;
  bool running = true;
  bool orbiting = false;
  bool panning = false;
  /// Middle-dragging the board around its own surface, in the geometry view.
  bool sliding = false;
  /// Whether the board was its own shape last frame, so the camera can be put round
  /// whatever it has just become.
  bool wasSurface = false;
  /// The camera choreography for a followed move on a shape (M17.19): hold, morph,
  /// approach, travel, morph home. Inactive on the flat board and in every capture.
  ShapeMoveSequence shapeSeq;
  auto lastFrame = std::chrono::steady_clock::now();
  float fps = 0.0f;

  while (running && !shell->quitRequested()) {
    SDL_Event e;
    // A board game has nothing to animate most of the time, so the loop waits for
    // input rather than spinning. While a piece is actually moving it wants frames, so
    // the wait shortens to roughly one.
    const bool animating = shell->hasGame() && shell->session()->animation().active();
    if (!SDL_WaitEventTimeout(&e, animating ? 8 : 100)) e.type = SDL_EVENT_POLL_SENTINEL;
    do {
      if (e.type == SDL_EVENT_POLL_SENTINEL) continue;
#ifdef CB_HAVE_IMGUI
      const bool consumed = (*ui)->processEvent(e);
#else
      const bool consumed = false;
#endif
      const app::Settings& settings = shell->settings();
      const bool inGame = shell->screen() == app::Screen::Game && shell->hasGame();

      switch (e.type) {
        case SDL_EVENT_QUIT:
          running = false;
          break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
          const auto w = static_cast<std::uint32_t>(e.window.data1);
          const auto h = static_cast<std::uint32_t>(e.window.data2);
          if (w > 0 && h > 0) {
            // The window rebuilds the swapchain and its per-image targets with it.
            (void)window->recreate(w, h);
          }
          break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
          if (consumed || !inGame) break;
          if (e.button.button == SDL_BUTTON_LEFT) {
            bool handled = false;
#ifdef CB_HAVE_IMGUI
            const app::Session* session = shell->session();
            if (optionsFor(*shell).surface && session != nullptr) {
              // The geometry view: pick against the same surface the renderer drew, with
              // the effective camera, so the click resolves to the cell under the cursor
              // (M17.3). A shot in flight means the camera is about to move, so the click
              // is refused - the same rule `clickPixel` already applies on the flat board
              // (M17.16). The ray is in the board rectangle's pixel space.
              handled = true;
              if (!session->shotInFlight()) {
                const render::PlaySurface surf = render::PlaySurface::build(
                    session->variant(), poseFrom(settings, session->variant()));
                app::Action a;
                a.kind = app::ActionKind::ClickCell;
                a.cell = surf.pick(session->cameraOver(surfacePlacements(surf),
                                                       surf.bounds(), view::ViewConfig{}),
                                   boardRect.width, boardRect.height,
                                   e.button.x - boardRect.x, e.button.y - boardRect.y);
                (void)shell->session()->apply(a);
              }
            }
#endif
            if (!handled) {
              // Picking uses the same rectangle the board was drawn into, or a click
              // would land on a different cell than the one under the cursor.
              shell->session()->clickPixel(e.button.x - boardRect.x,
                                           e.button.y - boardRect.y, boardRect.width,
                                           boardRect.height);
            }
          } else if (e.button.button == SDL_BUTTON_RIGHT) {
            orbiting = true;
          } else if (e.button.button == SDL_BUTTON_MIDDLE) {
            // On the shape, the middle button slides the board *along* the surface rather
            // than sliding the view about. Nothing is lost: the shape is framed and
            // centred the moment the view is switched on, so there is nothing to pan to -
            // and riding a1 round to b1, to c1, and eventually back to a1 is the one way
            // to feel a gluing rather than be told about it.
            if (inGame && optionsFor(*shell).surface) {
              sliding = true;
            } else {
              panning = true;
            }
          }
          break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
          if (e.button.button == SDL_BUTTON_RIGHT) orbiting = false;
          if (e.button.button == SDL_BUTTON_MIDDLE) {
            panning = false;
            sliding = false;
          }
          break;
        case SDL_EVENT_MOUSE_MOTION:
          if (sliding && shell->hasGame()) {
            // Drag the board round its own surface: left and right along the ranks, up
            // and down along the files. On a cylinder the ranks are free, so left/right
            // does nothing and up/down carries the board (M17.8). Roughly a cell per
            // seventy pixels, and the pose is a pure function of the number, so dragging
            // back retraces it with nothing remembered between frames.
            app::Settings& st = shell->settings();
            const VariantSpec& variant = shell->session()->variant();
            const bool ring = render::PlaySurface::slidesAlongRanks(variant);
            if (ring && !variant.geom.isOrientable()) {
              // On a non-orientable board the V axis is the only slide measured to keep
              // the tile corners abutting across the seam (the U slide's gap grew to
              // about ten times an ordinary one), so both drag axes turn V - up does what
              // left-right does.
              st.geometrySlideV += (e.motion.xrel + e.motion.yrel) * 0.014f;
            } else {
              if (ring) st.geometrySlideV += e.motion.xrel * 0.014f;
              st.geometrySlideU += e.motion.yrel * 0.014f;
            }
          } else if (panning && shell->hasGame()) {
            // Drag the board with the middle button: slide the look-at point in the
            // camera plane, scaled so it tracks the pixels at any zoom.
            app::Action a;
            a.kind = app::ActionKind::Pan;
            const float k = shell->session()->camera().distance * 0.0016f;
            a.dx = e.motion.xrel * -k;
            a.dy = e.motion.yrel * k;
            (void)shell->session()->apply(a);
          } else if (orbiting && shell->hasGame() && !shell->session()->flatView()) {
            // Orbiting a flat board would only tilt the diagram out of true.
            app::Action a;
            a.kind = app::ActionKind::Orbit;
            a.dx = e.motion.xrel * 0.01f * settings.orbitSensitivity;
            a.dy = e.motion.yrel * -0.01f * settings.orbitSensitivity *
                   (settings.invertOrbitY ? -1.0f : 1.0f);
            (void)shell->session()->apply(a);
          }
          break;
        case SDL_EVENT_MOUSE_WHEEL: {
          if (consumed || !inGame) break;
          app::Action a;
          a.kind = app::ActionKind::Zoom;
          const float step = 0.1f * settings.zoomSensitivity;
          a.dx = e.wheel.y > 0 ? 1.0f - step : 1.0f + step;
          (void)shell->session()->apply(a);
          break;
        }
        case SDL_EVENT_KEY_DOWN: {
          if (consumed) break;
          // In two-player mode the keyboard belongs to the players, not the shell: a key
          // a player owns is fed to their half of the board, and the single-player
          // shortcuts are off entirely so a stray key while the other player types cannot
          // undo or reset the game. Esc still pauses.
          const bool twoPlayer = inGame && shell->session()->hotSeat();
          if (twoPlayer && shell->session()->feedHotSeat(keyChar(e.key.key))) break;
          app::Action a;
          switch (e.key.key) {
            case SDLK_ESCAPE:
              // Esc is "step back one screen", which from the board means pause.
              shell->back();
              break;
            case SDLK_Q:
              if (shell->screen() == app::Screen::MainMenu) running = false;
              break;
            case SDLK_U:
              if (inGame && !twoPlayer) {
                a.kind = app::ActionKind::Undo;
                (void)shell->session()->apply(a);
              }
              break;
            case SDLK_R:
              if (inGame && !twoPlayer) {
                a.kind = app::ActionKind::Reset;
                (void)shell->session()->apply(a);
              }
              break;
            case SDLK_F:
              // Flat view, without a trip to the settings screen.
              if (inGame && !twoPlayer) {
                app::Settings& st = shell->settings();
                st.flatView = !st.flatView;
                shell->session()->setFlatView(st.flatView);
              }
              break;
#ifdef CB_HAVE_IMGUI
            case SDLK_G:
              // Turn the board into its own shape (M17), without a trip to the rail.
              // Only where there is a surface to become.
              if (inGame && !twoPlayer &&
                  render::hasPlaySurface(shell->session()->variant())) {
                app::Settings& st = shell->settings();
                st.geometryView = !st.geometryView;
              }
              break;
            case SDLK_LEFTBRACKET:
            case SDLK_RIGHTBRACKET:
              // The turn, for a keyboard: the same target the INVERT button sets, which
              // the front end then eases the pose towards (M17.7). `]` turns it inside
              // out, `[` brings it back.
              if (inGame && !twoPlayer && optionsFor(*shell).surface) {
                shell->settings().geometryInvert = e.key.key == SDLK_RIGHTBRACKET;
              }
              break;
#endif
            default:
              break;
          }
          break;
        }
        default:
          break;
      }
    } while (SDL_PollEvent(&e));

    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - lastFrame).count();
    lastFrame = now;
    if (dt > 0.0f) fps = fps * 0.9f + (1.0f / dt) * 0.1f;

    // The title names the game: the variant, and whose move it is.
    {
      std::string title = "ChessBox";
      if (!shell->currentVariant().empty()) title += " - " + shell->currentVariant();
      if (shell->hasGame()) {
        title += shell->session()->game().position().sideToMove() == Color::White
                     ? " - White to move"
                     : " - Black to move";
      }
      static std::string applied;
      if (title != applied) {
        SDL_SetWindowTitle(window->handle(), title.c_str());
        applied = title;
      }
    }

    // Stepping back off the board, and coming back to it. Held on the session so the
    // board, the flat board's pieces and the picking ray cannot disagree about where
    // the camera is.
    if (shell->hasGame()) {
      // The target comes from the shell: the board recedes at pause, and recedes again
      // one step further in at a pause panel. Stepping back to the board brings it in.
      const float want = shell->boardPullBack();
      steppedBack += std::clamp(want - steppedBack, -dt * 3.4f, dt * 3.4f);
      shell->session()->setPullBack(steppedBack);
      // Out of focus by the same amount the camera has stepped back, so the two read as
      // one movement. Past the pause it is already fully soft.
      renderer->setBlur(std::min(steppedBack, 1.0f));
    }

    // Ease the eversion towards the INVERT button's target, over about half a second. The
    // ramp lives here and not in the pose, so the pose stays a pure function of its
    // number and a still stays reproducible (M17.7).
    {
      app::Settings& st = shell->settings();
      const float target = st.geometryInvert ? 1.0f : 0.0f;
      const float step = dt * 2.0f;
      if (st.geometryEvert < target) {
        st.geometryEvert = std::min(target, st.geometryEvert + step);
      } else if (st.geometryEvert > target) {
        st.geometryEvert = std::max(target, st.geometryEvert - step);
      }
    }

    // The shape-follow choreography (M17.19): align, approach, travel, return. It drives
    // the piece's clock and the transient slide, and leaves the effective camera in
    // `shapeSeq` for `boardCamera`. On the flat board and in captures it stays inactive,
    // so the old follow behaviour is untouched.
    updateShapeSequence(*shell, shapeSeq, dt);

    // Set once the interface has built this frame: the surface view draws the board
    // itself, so the ordinary renderer stays out of the way (M17).
#ifdef CB_HAVE_IMGUI
    (*ui)->tick(dt);
    (*ui)->newFrame();
    const render::UiRequest request = (*ui)->build(*shell, fps);
    (*ui)->endFrame();

    if (request.quit) running = false;
    // Start whenever the library asks, even for the variant that is already loaded:
    // leaving a game and picking the same one again means "start over", and skipping it
    // because the name matched left the button dead. A request is one-shot - the library
    // is only built on its own screen - so this cannot restart a running game by itself.
    if (!request.loadVariant.empty()) {
      (void)shell->startGame(request.loadVariant);
      renderer->setOptions(optionsFor(*shell));
      // A new variant is a new shape, and `geometryView` is sticky across the load, so
      // frame unconditionally - a one-shot "did the toggle flip" check would miss this
      // and leave the camera on the previous variant's shape, or the new one's flat board
      // (M17.14). Set `wasSurface` too so the toggle check below does not re-fire.
      frameBoard(*shell);
      wasSurface = optionsFor(*shell).surface;
    }
    if (request.saveGame) (void)shell->saveGame(request.gameName);
    if (request.loadGame) {
      if (shell->loadGame(request.gameName).has_value()) {
        // The loaded game may be a different variant, so treat it like a variant load:
        // refresh the renderer and put the camera round whatever is now drawn.
        renderer->setOptions(optionsFor(*shell));
        frameBoard(*shell);
        wasSurface = optionsFor(*shell).surface;
      }
    }
    if (request.settingsChanged) {
      shell->applySettings();
      renderer->setOptions(optionsFor(*shell));
      (*ui)->setIconStyle(render::iconStyleFromName(shell->settings().pieceIcons));
      const view::Theme theme = view::themeFromName(shell->settings().theme);
      renderer->setTheme(theme);
      (*ui)->setTheme(theme);
      (void)window->setVsync(shell->settings().vsync);
    }
#ifdef CB_HAVE_IMGUI
    // The board has just become its own shape, or gone back to being a diagram. Either
    // way it is a different size in a different place, so the camera is put round it -
    // once, on the change, and never while the player is turning it.
    {
      const bool nowSurface = optionsFor(*shell).surface;
      if (nowSurface != wasSurface) {
        renderer->setOptions(optionsFor(*shell));
        frameBoard(*shell);
        wasSurface = nowSurface;
      } else if (nowSurface) {
        renderer->setOptions(optionsFor(*shell));
      }
    }
#endif
    if (request.toggleFullscreen) {
      SDL_SetWindowFullscreen(window->handle(), shell->settings().fullscreen);
    }
    if (request.boardRect[2] > 0) {
      const render::BoardRect wanted{request.boardRect[0], request.boardRect[1],
                                     request.boardRect[2], request.boardRect[3]};
      // Ease the board's rectangle towards where the screen says it belongs, so opening
      // or closing the pause menu slides the board aside instead of teleporting it.
      const float k = std::clamp(dt * 8.0f, 0.0f, 1.0f);
      const auto approach = [k](float& v, float to) {
        v += (to - v) * k;
        if (std::abs(to - v) < 0.5f) v = to;
      };
      approach(boardRect.x, wanted.x);
      approach(boardRect.y, wanted.y);
      approach(boardRect.width, wanted.width);
      approach(boardRect.height, wanted.height);
    }
#else
    const render::UiRequest request{};
#endif

    render::InstanceSet instances;
    view::OrbitCamera camera;
    if (shell->showsBoard()) {
      if (boardRect.valid()) {
        shell->session()->setBoardAspect(boardRect.width / boardRect.height);
      }
      syncMarks(*renderer, *shell->session());
      // The choreography drives the animation's own progress while it is running (the
      // piece is pinned through the lead-in and the return); advancing it here too would
      // fight that. Outside the choreography it advances as it always did.
      if (!shapeSeq.active) shell->session()->advanceAnimation(dt);
      instances = renderer->buildInstances(
          shell->session()->snapshot(), shell->session()->viewConfig(),
          &shell->session()->seams(), &shell->session()->animation(),
          [&](CellId c) { return shell->session()->boardVisible(c); },
          [&](CellId c) { return shell->session()->game().cellInPresent(c); },
          shell->session()->timelineLinks());
      camera = boardCamera(*shell, shapeSeq);
    }

    const auto overlay = [&](VkCommandBuffer cmd) {
#ifdef CB_HAVE_IMGUI
      (*ui)->record(cmd);
#else
      (void)cmd;
#endif
    };

    // Acquire a swapchain image and record the whole frame straight into it (ADR-0018):
    // no offscreen image, no blit. Two frames may be in flight, so the previous frame can
    // still be on the GPU while this one is recorded. A frame that comes back invalid had
    // the swapchain rebuilt under it; skipping one frame is cheap and correct.
    auto frame = window->beginFrame();
    if (!frame.has_value()) {
      std::fprintf(stderr, "begin frame failed: %s\n", frame.error().format().c_str());
      break;
    }
    if (frame->valid()) {
      if (auto ok = renderer->record(frame->commandBuffer, *frame->target, instances,
                                     camera, overlay, boardRect, frame->slot);
          !ok.has_value()) {
        std::fprintf(stderr, "render failed: %s\n", ok.error().format().c_str());
        break;
      }
      if (auto ok = window->submitFrame(*frame); !ok.has_value()) {
        std::fprintf(stderr, "submit failed: %s\n", ok.error().format().c_str());
        break;
      }
      if (auto ok = window->presentFrame(*frame); !ok.has_value()) {
        std::fprintf(stderr, "present failed: %s\n", ok.error().format().c_str());
        break;
      }
    }

#ifdef CB_HAVE_IMGUI
    // Rescaling rebuilds the font atlas, so it happens between frames, never inside one.
    if (request.applyScale) (void)(*ui)->setScale(shell->settings().guiScale);
#endif

    // Frame cap (M4.8): with vsync off the loop would redraw as fast as the CPU allows,
    // and every menu animates forever, so a static screen would still spin a core. Sleep
    // out the rest of the target period.
    if (const int cap = shell->settings().frameCap; cap > 0) {
      const float period = 1.0f / static_cast<float>(cap);
      const float spent =
          std::chrono::duration<float>(std::chrono::steady_clock::now() - now).count();
      if (spent < period) {
        SDL_Delay(static_cast<Uint32>((period - spent) * 1000.0f));
      }
    }
  }
  return 0;
}
