# M11 — The Move Camera: following a piece through any geometry

**Goal.** The board stops being a static diagram a piece moves *across* and becomes a
place a piece moves *through*. The view travels with the move — through a glued seam,
around a mirror, between slices — on any board and any topology. This is the visual
payoff of M3/M6: it is where "the engine computes the right thing" becomes something a
person can *see*, and it is what makes a 4-D or non-orientable game watchable rather
than merely playable.

**Exit condition.** On any shipped variant, at D ∈ {2,3,4,6}, with any topology, a move
plays with the camera keeping the moving piece and its route readable for the whole
move; it reorienting correctly across a glued or mirrored seam; it **cutting** rather
than panning when the move crosses a grid axis; and a capture is reproducible
frame-for-frame by `--shot` at a fixed `t`.

---

## The one idea: the camera reads only data the engine already generalised **[INVARIANT]**

This is the whole reason the feature is tractable, and it is the direct answer to "can
this be generalised like the games are?" A camera that followed *positions* would have
to know the topology and the dimension count, and would be wrong on both. Instead:

1. **The route is topology, already.** `view::MovePath` (`src/view/move_anim.hpp:43`) is
   a list of `PathStep`s, each tagged `Interior`/`Portal`/`Bounce` and carrying the
   **post-transport** `Direction`, the `faceAxis` and the `faceSide`. A Klein seam, a
   Möbius wrap and a mirror appear there only as a `kind` and a direction vector. The
   camera never decodes a `Coord` and never consults an identification.

2. **The world positions are dimension, already.** `view::layout` (`src/view/layout.hpp:79`)
   has already placed every cell — *including cells on grid axes, i.e. whole
   sub-boards* — into world space. A move six dimensions away has a world start and a
   world end. Following those endpoints follows the move in 6-D exactly as it does in
   2-D. There is **no `if (dims == …)` anywhere in the camera.**

3. **The orientation is derived, never authored.** The pose "behind the piece, looking
   along travel, up is board up" is built from the run's transported `Direction`. On a
   non-orientable seam that direction is different, so the camera turns with the piece
   for free. This is the *same* invariant that makes the moves correct — `transport the
   direction, not just the position` (ARCH §4.1) — and it must not be re-derived by hand.

4. **The genuinely ambiguous parts are policy, not special cases.** Whether a portal is
   a hard cut, a fade through the iris, or an orbit that shows both ends; whether a leap
   is followed or framed; whether a grid-axis move pans or cuts — each is a documented
   choice with a default, exactly as "what is forward for a pawn on a Klein bottle" is a
   per-variant policy (ARCH §4.1, M3). **[INVARIANT]**

So the generalisation is structural, not aspirational: the camera is a **pure function of
`(MovePath, layout, ViewConfig, CameraPolicy, t)`**, and every input is already correct
in every geometry and every dimension.

### The correspondence with how the games are generalised

| games are generalised by | the camera is generalised by |
|---|---|
| `VariantSpec` — declarative data | `CameraPolicy` — declarative data |
| one `MoveAtom` algebra | one pose algebra (`target, distance, yaw, pitch`) |
| the transition group — derived | orientation from the transported `Direction` — derived |
| per-variant policy for ambiguity | per-geometry/per-view policy for portal, leap, grid axis |
| the naive movegen oracle (ADR-0009) | the naive *frame-the-cell* oracle (M11.2) |
| differential/golden tests per variant | golden poses per geometry, headless (M11.7) |

---

## M11.0 Preconditions and placement

Depends only on M4 (renderer + `MoveAnimation`) and M6 (temporal axes and the multiverse
view), both done. It does **not** depend on M7/M8/M9/M10. It is **first in the roadmap's
release sequence** (Wave 1, the clippable hook): it is the single most promotable feature
and nothing blocks it. The number M11 is an append-only ID, not a position. It must not
force M7's authoring document to exist, but it should leave a clean seam for M12 and for
an authored camera preset.

---

## M11.1 The pose algebra and the policy record [ADR-0016, ADR-0017]

The camera answers exactly two questions: *where is it*, and *what is it allowed to do*.
Separate them.

### M11.1.1 The pose

```cpp
namespace cb::view {

/// A camera at one instant. Deliberately the same fields OrbitCamera already has, so a
/// pose is applied by assignment and there is no second notion of "a camera".
struct CameraPose {
  Vec3  target{};
  float distance{10.0f};
  float yaw{0.6f};
  float pitch{0.9f};
  /// 0 = follow the board's up; nonzero rolls the view for a loop or a rolled axis.
  float roll{0.0f};
};

struct CameraShot {
  float t0{0};        // normalised start time within the move
  float t1{1};        // normalised end time
  CameraPose a{};     // pose at t0
  CameraPose b{};     // pose at t1
  Ease ease{Ease::Smooth};
  ShotKind kind{ShotKind::Track};  // Track | Frame | Cut | Portal
};

}  // namespace cb::view
```

A move camera is then a `std::vector<CameraShot>` — a small directed sequence. That is
the entire output vocabulary; a view style is a different sequence, not new code.

### M11.1.2 The policy

`CameraPolicy` is data, defaulted *per geometry kind and per view style*, and overridable
per variant (a future M12 authoring path). It never contains a dimension or a topology —
only the choices the engine cannot make for the player:

```cpp
struct CameraPolicy {
  FollowMode follow{FollowMode::Route};  // Off | Piece | Route | Both
  float lead{0.28f};        // how far ahead of the piece the target sits, in cells
  float pull{0.55f};        // extra distance as speed rises, in cells
  float minDistance{3.0f};  // never dolly closer than this
  float deadline{0.55f};    // seconds a cut may take; longer moves become shots
  PortalPolicy portal{PortalPolicy::Fade};
      // Cut | Fade | Orbit  -- what to do at a StepKind::Portal
  BouncePolicy bounce{BouncePolicy::Hold};
      // Hold | Recoil         -- what to do at a StepKind::Bounce
  LeapPolicy leap{LeapPolicy::Frame};   // Follow | Frame
  GridAxisPolicy grid{GridAxisPolicy::Cut};  // Pan | Cut  -- crossing a grid axis
};
```

The defaults are chosen so that the *identity* policy (`follow=Off`) reproduces today's
behaviour exactly, which makes the change a strict superset and keeps every existing
`--shot` and golden valid. The defaults are keyed off data the view already has:
`SeamKind` for the portal look, `ViewConfig` for whether an axis is a screen axis or a
grid axis, and `MovePath::leap` — **never** a dimension count.

**ADR-0016 "The move camera is a pure function of the generalised move trace."**
It records the four invariants above, why the camera lives in `view` and not in
`render`, and why the route is consumed as `MovePath` rather than re-decoded.

**ADR-0017 "Camera policy is data, keyed by geometry kind, not by dimension."**
It records the policy record, its defaults, the per-variant override, and the rule that
no policy branch may test the dimension count.

---

## M11.2 The pure camera function and its oracle

One function produces the pose; it is pure, headless and testable with no GPU:

```cpp
/// The pose of the move camera at progress t in [0,1], or the settled frame pose for a
/// move whose policy is FollowMode::Off.
CameraPose moveCamera(const MovePath& path,
                      const std::vector<Placement>& placements,
                      const ViewConfig& cfg,
                      const Bounds& scene,
                      const CameraPolicy& policy,
                      float t);
```

**The route is decomposed once, for both consumers.** `MoveAnimation` already splits a
move into straight `Run`s separated by portals (`src/view/move_anim.cpp`). Extract that
into a shared

```cpp
struct RouteRun {
  std::vector<Vec3> points;   // world-space polyline of this run
  float t0, t1;               // normalised span
  StepKind endedWith{StepKind::Interior};
  std::uint8_t faceAxis{0};
  Side faceSide{Side::Max};
};
std::vector<RouteRun> routeRuns(const MovePath&, const std::vector<Placement>&);
```

and have the animation *and* the camera consume it, so "what a run is" has exactly one
definition (the same discipline `layout` has). This is a refactor with a differential
test: the animation's sampled positions must be byte-identical before and after.

**The oracle.** `tests/oracle/` gets a deliberately dumb camera oracle: at each step
endpoint `t`, its pose is *frame the destination cell exactly*, with no lead, no pull,
no easing. `moveCamera` must (a) equal the oracle at every `t` where a `Frame` shot
begins or ends, and (b) satisfy the **safe-zone invariant**: for all `t`, the moving
piece's world position projects inside the board rectangle when `follow != Off`, and
both endpoints of the current run project inside it. Every smoothing, lead or pull term
is a deviation proven to stay inside that envelope — the camera's equivalent of ADR-0009.

**Determinism.** `moveCamera` reads no clock and no previous frame (the overture rule,
`src/render/overture_scene.hpp:92`). Two calls at the same `t` are identical; a
`--shot` at a pinned `t` reproduces byte-for-byte. This is what lets M11.7 compare
golden frames.

---

## M11.3 Geometry-general: portals, bounces and wraps

This is the part that is *only* possible because the engine already transports
directions, and the part a position-following camera gets wrong.

- **A glued portal** (`StepKind::Portal`) ends a run. The camera runs the `PortalPolicy`:
  - `Cut` — hard cut to the far side at `t` of the crossing, matching the existing iris;
  - `Fade` (default) — the piece sinks into the portal and rises on the far side
    (`MoveAnimation` already animates this, `src/view/move_anim.cpp`), and the camera
    eases its target across the seam in the same window;
  - `Orbit` — the camera holds and *rotates* to show both the leaving and the arriving
    end at once, using the pair `(faceAxis, faceSide)` and the seam's partner from
    `SeamMap` (`src/view/seams.hpp:32`). This is the "show the audience the trick" mode.
- **The arrival orientation is read, not guessed.** After a crossing, the next run's
  travel direction is `PathStep::dir` *after* transport. The camera's yaw is built from
  it, so on a Klein seam the camera comes out rotated with the piece instead of swerving.
  A test asserts this: a rook crossing the Klein file seam has a camera that has turned,
  and the turn equals the transport of its start direction.
- **A mirror** (`SeamKind::Mirror`, `partner == cell`) is a `Bounce`: with `Hold`
  (default) the camera stops at the wall, the piece reflects, and the camera never
  translates through a face that has nothing behind it. This is the one place a
  position-following camera would fly off the edge of the world.
- **A torus / cylinder wrap** has no special case at all: it is a `Portal` with a
  partner, and it falls out of the same code as Klein. If it needs a branch, the design
  is wrong.

**The naming is the point.** `Portal`, `Bounce`, `Mirror`, `partner`, `SeamKind` are the
engine's own topology vocabulary. The camera speaks it and adds no new words.

---

## M11.4 Dimension-general: grid axes, slices and cuts

`ViewConfig` (`src/view/layout.hpp:22`) draws up to three screen axes spatially and makes
every remaining axis a **grid** axis — a lattice of sub-boards, which is how 4-D, 6-D and
the turn/timeline axes are shown. A move along a grid axis therefore travels between
sub-boards, and the camera has to choose:

- `GridAxisPolicy::Pan` — translate the target across the scene with the move. Correct
  and continuous, but across a 6-D lattice it is a long, disorienting dolly.
- `GridAxisPolicy::Cut` (default) — the camera *cuts* between the source slice and the
  destination slice at the crossing, reusing the destination seam's colour (the portal
  ramp, `src/view/seams.cpp`) to carry continuity. This is the readable choice and the
  one that keeps a `t6` move from becoming a smear.

The decision is **derived from `ViewConfig`** (is the crossed axis a screen axis or a
grid axis?) and from `MovePath` — never from the dimension count. A move on a screen
axis is a track; a move on a grid axis is a cut. The same code then handles a 4-D
hypercube, a 6-D torus and the timeline axis of `5d`, because all three are grid axes.

**Readable, not faithful.** The faithful whole-lattice projection is unreadable above
3-D; the camera deliberately shows the active slice and its glued neighbours, and hides
the rest — a view-layer policy, stated as such.

---

## M11.5 Integration: one effective camera, and interaction that does not lie

The camera is owned by `app::Session` (`src/app/session.hpp:191`) and read through
`Session::camera()` (`src/app/session.cpp:230`), which already folds in the pause
pull-back. The move camera becomes a second, time-varying contribution to that same
accessor, so **every** render consumer — the board renderer, the coordinate labels, the
flat-view tokens — agrees by construction.

- The effective camera is `settled ⊕ shot(t)` where `settled` is the player's orbit pose
  and the shot is expressed as an **offset in the shot's own frame**, not an absolute
  pose. A player who has orbited the board keeps their orbit; the move camera leads it.
  `FollowMode::Off` is the zero offset and is bit-identical to today.
- **The pick-ray must follow the rendered camera, or clicking breaks.** Today
  `Session::clickPixel` uses the raw member (`src/app/session.cpp:558`), not the
  effective camera; the pause pull-back happens to keep them equal because
  `pullBack_ == 0` in play. A move camera animates during play, so picking during a
  cinematic would be desynchronised — the class of bug the `Move::captureCell` gotcha
  already taught. The fix is explicit, and is a stated test: **while a shot is in
  flight the board is interaction-locked; when it settles, picking resumes from the
  settled pose.** No invisible drift.
- **`--shot` pins the shot.** Captures never advance animations today
  (`src/gui/main.cpp`), so the move camera is driven by a fixed `t` exactly as the
  overture player is pinned (`app::OverturePlayer::setProgress`). `--shot --move-t 0.5`
  renders the half-way pose reproducibly.
- **Settings.** A `cameraMode` (Off / Piece / Route), plus `cinematicCuts` and
  `followStrength`, joins `app::Settings` beside the existing camera keys
  (`src/app/settings.hpp:52`), parsed/saved/sanitized the same way, applied through
  `Shell::applySettings` (`src/app/shell.cpp:250`). Off is the default so the shipped
  behaviour is unchanged.

---

## M11.6 The cinematic layer: readable is not the same as pretty

A camera that chases every step is nauseating and hides the board. The cinematic layer
is the part that makes it watchable, and every element of it is a testable invariant:

- **Lead and pull.** The target leads the piece by `lead` cells and the camera pulls back
  by `pull` as speed rises, so a long slide is seen from further out and a short step is
  seen up close. Bounded by `minDistance`.
- **Shots, not one long lerp.** `CameraShot`s are planned from the run decomposition: a
  short move is one `Track`; a long or portal-crossing move is several shots with cuts,
  so the camera never travels further than `deadline` worth of time in one take.
- **The safe zone.** A rectangle (a fraction of the board rect) that the moving piece
  must stay inside for every `t`; the oracle test in M11.2 enforces it. This is the
  camera analog of "the move must stay legal" — an invariant, not a hope.
- **Follow-the-last-move replay.** A one-button replay that re-runs the last move's shot
  sequence from a fixed `t=0`, so a player can see what just happened. Deterministic and
  `--shot`-able.
- **Cinema chrome.** Hiding the rails, ledger and highlights for the duration of a shot,
  so a clip shows the board and the move and nothing else. The renderer already takes a
  `boardRect` and an overlay (`src/render/board_renderer.hpp:122`); this is a UI policy
  over that seam.
- **The frame budget is a precondition, not a detail.** A shot animates during play, so
  every frame it is drawn is a frame the board and the interface are drawn in too. The
  shipped loop is fully CPU/GPU-serialised and the library overtures are CPU geometry
  (M4.8/M4.9), so a camera that lands on top of them is the wrong place to discover a
  frame-time problem. M11 assumes M4.8's pipelining and M4.9's (or M13's) cheaper menu
  geometry; if those slip, the move camera is where a slow frame shows first.

---

## M11.7 Tests and goldens

All camera logic is in `view`/`app` and tested with **no GPU**, mirroring how the
renderer's instance building is tested headlessly.

- **Purity/determinism:** `moveCamera(t)` twice at the same `t` is bit-identical; no
  clock, no previous frame.
- **Oracle differential:** `moveCamera` equals the frame-the-cell oracle at every
  `Frame` shot boundary, for every shipped variant and a sweep of moves.
- **Safe-zone property:** for random legal moves on random variants, the moving piece
  projects inside the board rect for all sampled `t` when `follow != Off`.
- **Topology:** a Klein-seam crossing turns the camera by exactly the transported
  direction; a mirror never translates the camera through the wall; a torus wrap is a
  `Portal` and produces the same shot kinds as Klein. Each is a named unit test.
- **Dimension:** the same move replayed at D=2,3,4,6 changes only the world positions,
  not the shot *kinds*; a grid-axis crossing produces a `Cut` and a screen-axis crossing
  a `Track`. This is the generalisation claim, pinned.
- **Golden frames:** `--shot` of a chosen move at `t=0, .25, .5, .75, 1` for `klein`,
  `cube5`, `hyper4` and `t6`, compared against committed images (with the existing
  validation-layer exit code). A change to the camera must explain itself in the commit,
  per the golden rule.
- **Interaction:** while a shot is in flight, a click does nothing; after it settles,
  picking resolves to the same cell as the settled pose. This is the `clickPixel` test.
- **Refactor safety:** the `routeRuns` extraction leaves `MoveAnimation`'s sampled path
  byte-identical (differential test).

---

## M11.8 Docs and skills

- `docs/camera.md` — the pose algebra, the policy record, and worked examples: a
  cylinder track, a Klein turn, a mirror bounce, a `cube5` depth move and a `t6`
  grid-axis cut.
- Update `docs/ARCHITECTURE.md` §10: the move camera as part of the projection pipeline,
  reading `MovePath`/`layout` only.
- Update `AGENTS.md`: "how a menu object answers a drag" gains "how a move answers the
  camera", and the gotcha list gains the click-picking desync and the "no dimension in
  the camera" rule.
- New skills after the third repetition: `cb-camera-policy`, `cb-camera-shot`.

---

## Dependencies and ordering

1. **M11.1** the model and the policy record (ADR-0016/0017) — all else hangs off it.
2. **M11.2** the pure function + oracle + the `routeRuns` refactor — the largest and the
   one whose value the rest depends on.
3. **M11.3** portals/bounces — geometry generality, testable without a renderer.
4. **M11.4** grid axes and cuts — dimension generality.
5. **M11.5** integration, picking, settings — needs 2–4.
6. **M11.6** the cinematic layer — needs 5.
7. **M11.7/M11.8** tests and docs land with each step, not at the end.

Each step is committed separately with its tests green; `ctest --preset dev` and
`tools/precommit.sh` between steps (the `cb-tdd-step` loop).

## Acceptance facts

1. Watching any shipped variant, a move's camera keeps the moving piece and its route
   inside the board rect for the whole move (the safe-zone property).
2. A piece crossing a Klein seam turns the camera by the transported direction, and a
   test asserts the turn equals the transport of the start direction.
3. A mirror never moves the camera through the reflecting wall.
4. A grid-axis move (a `t6` depth move, a `5d` timeline move, a `cube5` depth move)
   produces a cut, and a screen-axis move produces a track — from `ViewConfig`, not from
   the dimension count.
5. `follow = Off` is bit-identical to the pre-M11 camera, so every existing golden holds.
6. `--shot` at a pinned move-`t` reproduces frame-for-frame, and a change to the camera
   fails a golden until it is explained.
7. While a shot is in flight, clicking the board does nothing; when it settles, picking
   matches the settled pose.
8. A short move is one shot and a long multi-portal move is several shots, each within
   the `deadline`; no single take travels further than the policy permits.
9. A recorded move plays at the display rate on `klein`, `cube5`, `hyper4` and `t6`, with
   the board and the camera in the same frame, and `--shot --move-t` stays reproducible -
   backed by the M4.8 frame-time benchmark, not by eye.

## Risks and non-goals

- **Motion sickness is the real failure mode**, not correctness. The safe zone, the
  lead/pull bounds and the shot deadline exist because "the camera follows the piece"
  and "this is watchable" are different requirements. If they conflict, watchable wins.
- **The picking desync is the sharp edge.** `clickPixel` using the raw member is a latent
  bug the moment the camera moves in play; it is fixed in M11.5, not discovered in M12.
- **Do not re-derive topology in the camera.** Every temptation to special-case "Klein"
  or "3-D" is a signal that the data (`PathStep::kind`, `dir`, `ViewConfig`) is being
  ignored. The architecture test in M11.7 pins the absence of dimension branches.
- **Non-goals:** no free-fly camera, no scripting of camera moves, no per-move authored
  keyframes in M11 (that is M12 territory if it is wanted), no change to movegen,
  geometry or the temporal model. The camera observes; it never computes a rule.

## Status

Planned, not started. The design rests entirely on machinery that already exists and is
tested: `MovePath`/`tracePath` (topology as tags), `layout`/`Placement` (dimension as
world positions), `SeamMap` (portal partners and colour), `MoveAnimation` (run
decomposition), and `Session::camera()` (the single choke point). No new engine concept
is introduced; the milestone is a `view`/`app` change with an oracle and goldens.
