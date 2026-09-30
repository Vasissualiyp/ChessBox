# The move camera

The board stops being a static diagram a piece moves *across* and becomes a place a piece
moves *through*. The view travels with a move - through a glued seam, off a reflecting
wall, between the sub-boards of a grid axis - on any board, in any dimension, with no
per-geometry code. See [M11](plan/M11-move-camera.md) for the plan and ADR-0016/0017 for
the decisions; this page is the reference for the code that ships.

## The one idea

The camera reads only data the engine has already generalised:

- `view::MovePath` (`src/view/move_anim.hpp`) - the route, as `PathStep`s tagged
  `Interior`/`Portal`/`Bounce`, each carrying the **post-transport** `Direction`, the
  `faceAxis` and the `faceSide`.
- `view::Placement` (`src/view/layout.hpp`) - every cell already placed in world space,
  including whole sub-boards on a grid axis.

It never decodes a `Coord`, never consults an identification, and contains **no branch on
dimension count or topology**. A Klein seam is a `Portal`; a mirror is a `Bounce`; a `t6`
depth move is a run of interior steps between two world points that happen to be far
apart. That is the whole reason one function covers 2-D, 6-D, glued and mirrored boards.

## The types (`src/view/move_camera.hpp`)

```cpp
struct CameraPose { Vec3 target; float distance, yaw, pitch, roll; };
struct CameraShot { float t0, t1; CameraPose a, b; Ease ease; ShotKind kind; };
enum class FollowMode { Off, Piece, Route, Both };
struct CameraPolicy {
  FollowMode follow;  float lead, pull, minDistance, deadline;
  PortalPolicy portal; BouncePolicy bounce; LeapPolicy leap; GridAxisPolicy grid;
};
```

`CameraPose` is the same fields `OrbitCamera` has, so a pose applies by assignment
(`toOrbit`). `roll` has no `OrbitCamera` field yet and is dropped.

## The functions

- `routeRuns(path, placements) -> std::vector<RouteRun>` decomposes a route into straight
  runs, split at every non-interior step. A run ends at the cell it was entered from; the
  next begins on the far side, carrying the post-transport direction. A run whose `dir`
  has support on a `ViewConfig` **grid** axis is a grid-axis run.
- `moveCamera(path, placements, cfg, scene, policy, t) -> CameraPose` is pure in `t`: no
  clock, no previous frame. With `follow == Off` it returns the settled board framing (a
  strict no-op, so every pre-M11 golden holds). Otherwise the target leads the moving
  piece by `policy.lead`, looks along the run's transported direction, and pulls back with
  travel, bounded by `policy.minDistance`. A grid-axis run with `GridAxisPolicy::Cut`
  frames the source slice before the crossing and the destination slice after, instead of
  dollying across the empty gap.

**The oracle.** With `lead = 0`, `pull = 0` and linear time, at a run boundary the target
is the piece's own world position. Every smoothing term is a deviation from that; the
tests in `tests/unit/view/test_move_camera.cpp` pin it, plus purity, the transported turn,
and the grid-axis cut.

## How it reaches the screen (`src/app/session.cpp`)

`Session::camera()` folds the shot in as an **offset in the shot's own frame**: the pose
`moveCamera` would use with following off is subtracted first, then the difference is
scaled by `followStrength` and eased in and out at the ends of the move. So the player's
own orbit, pan and zoom survive, and the move camera only leads them.

- Off is the default (`Settings::cameraMode == "off"`), so the shipped look is unchanged.
- `Session::shotInFlight()` is true while a shot owns the view; `clickPixel` returns
  nothing then, and otherwise uses the **effective** camera, so the pause pull-back cannot
  desync picking.
- `MoveAnimation::progress()` is the shared clock: the animation and the camera read the
  same number, so they cannot disagree about where the piece is.

Settings: `cameraMode` (`off`/`piece`/`route`) and `followStrength`.

## Capturing a shot

`--shot FILE [--screen NAME] [--t]` renders one frame and `--clip DIR [--frames N --t0 a
--t1 b]` renders a deterministic sequence; the capture path feeds a fixed timestep and
states each frame's `t` outright. A clip frame at time `t` is byte-identical to the
`--shot` at the same `t`.

## Extending it

A new camera *choice* is a field on `CameraPolicy` (data); a new *geometry* is a new
`SeamKind`/`StepKind`, which the camera already speaks; a new *view style* is a
`ViewConfig`. None of them is a branch on `dims`. If a change needs to know whether the
board is 3-D or a Klein bottle, the data is being ignored - fix that instead.
