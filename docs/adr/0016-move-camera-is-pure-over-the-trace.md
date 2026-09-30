# ADR-0016 — The move camera is a pure function of the generalised move trace

**Status:** Accepted

## Context

The pieces and the rules were generalised before the camera was. A move is a `MovePath`:
a list of `PathStep`s, each tagged `Interior`/`Portal`/`Bounce` and carrying the
**post-transport** `Direction`, the `faceAxis` and the `faceSide` (`view/move_anim.hpp`).
`view::layout` has already placed every cell - including the whole sub-boards of a grid
axis - into world space. Nothing about a move's *shape* is dimension- or topology-specific
by the time it reaches the view.

A camera that followed *positions* would have to work out what a wrap or a twist means,
and would need a dimension count to do it; that is exactly the special-casing the rest of
the engine avoids. A camera that follows the *trace* does not: a Klein seam is a `Portal`
with a transported direction, a mirror is a `Bounce`, a `t6` depth move is a run of
interior steps between two world points that are simply far apart.

## Decision

`view::moveCamera(path, placements, cfg, sceneBounds, policy, t)` is a pure function of
the generalised trace, living in the `view` layer (L60), alongside `layout` and `OrbitCamera`.

- It reads `MovePath`, `Placement`, `ViewConfig`, `Bounds` and `CameraPolicy`, and nothing
  else. It never decodes a `Coord`, never consults an identification, and contains **no
  branch on dimension count or topology**. A test asserts the absence of dimension branches.
- The route is decomposed into straight runs by `view::routeRuns(path, placements)`, which
  the animation and the camera **share**, so "what a run is" has one definition. A portal or
  a bounce ends a run; the next begins on the far side.
- The orientation comes from the run's travel direction, which is the engine's transported
  direction read as a vector. On a non-orientable seam the direction is different, so the
  camera turns with the piece for free - the same invariant that makes the moves correct.
- It is **pure in `t`**: no clock, no previous frame. Two calls at the same `t` are
  identical; reverse playback is a falling `t`; a `--shot` at a pinned `t` is reproducible.
- `FollowMode::Off` returns the settled board framing unchanged, so adding the camera is a
  strict superset and every pre-M11 golden still holds.

The camera lives in `view`, not `render`, for the same reason `OrbitCamera` does: it is
presentation maths with no Vulkan in it, so it is testable with no GPU, and framing and
picking cannot disagree.

## Consequences

- Geometry and dimension generality are inherited, not re-implemented: one function covers
  2-D, 6-D, glued, mirrored and temporal boards.
- The camera is golden-testable headlessly (purity, an oracle, a safe-zone property), which
  is where camera bugs are actually found.
- The route is built per call by scanning `Placements`; the caller may cache the runs for a
  move (the animation already does the equivalent once per move).
- A camera that travels *with* a piece can make a player motion-sick, so the cinematic
  layer's lead/pull bounds and safe zone are part of the same decision, not a later one.

## Alternatives considered

- **Special-case per topology/dimension.** Rejected: it duplicates the transition model in
  the view and drifts from the engine the moment a new geometry lands.
- **Camera in `render`.** Rejected: it would make framing and picking untestable without a
  device and would put presentation policy above the projection.
- **Follow positions, derive orientation from the endpoint difference.** Rejected: on a
  glued board the difference between two cells means nothing, and it cannot turn the camera
  through a Möbius seam.

## How to reverse this

Delete `move_camera.*` and drive `OrbitCamera` as before; `FollowMode::Off` already produces
the pre-M11 framing, so nothing depends on the camera's presence unless it is selected.
