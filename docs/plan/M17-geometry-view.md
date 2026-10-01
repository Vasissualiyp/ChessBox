# M17 — Play on the shape: the geometry view

**Goal.** The board stops being a *representation* of a topology on a square grid and
becomes the topology. Press a button and the flat 8×8 becomes the object the variant is
actually played on - the files roll into a cylinder, the cylinder closes into a torus, the
rank edges pinch into a Möbius band or a Klein bottle, a mirror box grows its silvered
walls - and you keep playing on it. Turn it off and it lies flat again. The shapes already
exist for the New Game screen (`render/overture_scene.cpp`); this makes the *play* board
be the same shape, so "the a-file and the h-file are the same edge" is not a cyan line you
have to believe but a place you can point at.

**Exit condition.** On any shipped glued variant, a button (and a key) morphs the play
board into its own embedding, keeps the position and the move valid on it, lets a move be
made by picking a cell on the surface, and morphs back to the flat board; `standard` and
every non-glued variant are unchanged.

---

## M17.0 Why this is not the overture

The overture animates a fixed, authored scene: a separate 8×8 built for the menu, no
`Session`, no legal moves, no picking. M17 draws the *live* position on the warped surface.
The two share the warps and nothing else, and the separation is what keeps the overture
cheap and the game honest: M17 must never invent a move or a cell, and the overture must
never read the engine.

Depends only on M3 (the identification model), M4 (the renderer and `view::layout`) and the
M13.2 warps, all done. It is **Wave 1, before M16.1** in the release sequence, because it is
the most clippable thing the game can do with a board - a trailer that shows chess being
played *on a donut* is the store page's whole argument, and it needs no opponent and no
campaign.

---

## M17.1 The surface, from the variant

`app::overtureSignature` (M13.1) already turns a resolved spec into a `SurfaceKind`. M17
reuses it: the play board's embedding is `render::derivedSurfaceAt(kind, u, v)` (M13.2),
the exact surfaces the library screen already draws, asserted pointwise-equal to the
hand-authored ones. No new geometry; a variant that glues two axes is a torus whether it is
in a menu or a game.

## M17.2 The cell on the surface

- Each cell's centre is placed by the surface at its lattice `(u, v)`, exactly as the
  overture places its cells; its four corners come from `(u, v)` at the cell's own corners,
  so a cell bends with the surface instead of floating over it.
- Pieces ride their cell's normal (the finite-difference normal the overture uses), so a
  rook on the far side of a torus stands up outwards, not into the surface.
- The flat board is `t → 0`; the morph is the same blend the overture uses, driven by a
  UI clock rather than by the overture player.

## M17.3 Playing on it

This is the part that is engine work, and the part to get right:

- **Picking.** `Session::clickPixel` currently ray-casts against the axis-aligned cell boxes
  of the flat layout (`view::pickBox`). On the surface the cells are warped, so picking
  either ray-casts against the *curved* cell quads, or inverts the warp per cell and casts
  in lattice space. Whichever is chosen, the invariant is the one ADR-0011 already set:
  **the ray follows what was drawn**, or a click selects a different cell than the one under
  the cursor. The move camera (M11) already reads the trace and not the topology, so the
  route a move takes is drawn on the surface for free once the cells are.
- **Labels and marks.** Coordinates, the selection ring, legal-move highlights and the last-
  move mark are drawn on the surface cells, not in the flat layout.
- **The seams.** The edges the geometry glued keep their portal hue (as `view::seams` already
  gives them), so the surface and the seam colour agree.

## M17.4 Higher dimensions

For D ≥ 3 the "shape" is not an embedding but the lattice itself. M17 reuses the derived
D≥3 grid (M13.4) and the `view::layout` the game already draws: the toggle extrudes the
board along its depth and grid axes rather than warping it. This is a smaller step than the
2-D surfaces and mostly already exists.

**Superseded by M17.12**, which asks for the real shapes above two dimensions rather than
the lattice: `torus3d` as nested shells, `hyper4` as a hypercube, `t6` as the quintic.

## M17.5 The control and the setting

- A `Settings::geometryView` (`flat` | `surface`) beside `flatView`, and a button and a key.
  The identity is `flat`, so the shipped look is a strict no-op and every existing golden
  still holds.
- The morph is a pure function of a progress value, like the overture, so it is
  `--shot`-able and reversible; the front end owns the clock.
- `standard` and every non-glued variant stay flat: there is no surface to become, and a
  setting that did nothing there would be a lie.

## Tests and acceptance

- **The surface is the one the overture draws:** the play board at `t = 1` for `cylinder`,
  `torus`, `mobius` and `klein` uses `derivedSurfaceAt`, and a test asserts the cell centres
  land exactly where the overture's do for the same `(u, v)`.
- **Picking agrees with drawing:** for every cell, project its centre with the *effective*
  camera and pick that pixel back; it selects the same cell. On a glued board too.
- **`flat` is a strict no-op:** with `geometryView = flat`, every existing render golden and
  the flat-layout picking tests are unchanged.
- **The position is not touched:** toggling the view changes nothing in `VariantId`, the
  hash, the move list, or the FEN; it is presentation, like `shape`/`height`.
- **`--shot`:** `chessbox_gui torus --shot t.ppm --geometry surface` renders the board as a
  torus, validation-clean, and `--clip` morphs it.

### Acceptance facts

1. A button turns the play board into its variant's shape and back, and a move is playable
   on the shape.
2. Picking on the surface selects the cell under the cursor, on a glued board as on a flat
   one.
3. `standard` is bit-identical; a glued variant's true colour/edge semantics still hold.
4. The morph is a pure function of `t`, so a still is reproducible and the reverse is free.

## Dependencies and ordering

1. **M17.1/M17.2** the surface and the cells (reuse M13) — the picture.
2. **M17.3** picking and marks — the part that makes it a *game* and not a diorama.
3. **M17.4** the D≥3 lattice.
4. **M17.5** the setting, the button and the key.
5. Tests land with each step.

## Risks and non-goals

- **Picking on a curved, possibly non-orientable surface is the whole risk.** A wrong
  inverse picks the wrong cell and the view becomes untrustworthy; the test above is the
  gate. If an exact inverse is out of reach, ray-cast the drawn quads, which cannot disagree
  with the drawing because it is the drawing.
- **Readability.** A warped board is harder to play on than a diagram, which is why the flat
  view stays the default and the surface is a deliberate mode, not a replacement.
- **Non-goals:** no change to movegen, geometry or the temporal model; the toggle is
  presentation and must not enter `VariantId`; no free-form reshaping of authored variants;
  the surface is derived from the identifications, never drawn by hand.

---

## M17.7 - M17.18 The next increment: a shape you can handle

M17 shipped a board you can play on its own surface, and playing on it turned up six
things the first pass does not do. They are specified here, smallest first; each names what
is wrong, what is known about why, what to build and what pins it. M17.13 and M17.14 were
added after M17.7-M17.12 shipped, diagnosing two regressions the M17.12 first pass
introduced - see each section's own **Have** for the evidence. M17.15-M17.17 are a third
batch, asked for once a move could be made on the shape at all: the piece's own travel, the
move camera following it there, and keeping that camera from looking through the shape's
own geometry to find it. M17.18 is a fourth, a defect found by playing with the slide on
`klein` specifically.

### M17.7 An INVERT control, and pieces that stay on the outside

**Want.** A button in the game rail, beside `SHAPE`, that turns the shape inside out and
back: what was inside the hole is outside, and what was outside is in.

**Have.** `SurfacePose::evert` already is that turn, and it is already continuous, pure and
capturable (`--evert`, `[`/`]`). What is missing is the control and - the part that makes
it *play* rather than just look - what happens to the pieces.

**Build.**

- A `INVERT` button in `src/render/ui.cpp`'s game rail, gated exactly as `SHAPE` is
  (`hasPlaySurface`), lit while inverted. It sets a *target* of 0 or 1; the front end
  (`src/gui/main.cpp`, beside `steppedBack`) eases `Settings::geometryEvert` towards it
  over about half a second. The ramp lives in the front end and not in `PlaySurface`,
  because a pose that read a clock would stop being reproducible - the same split as
  `app::OverturePlayer` and `overtureScene`.
- **The outward side has to follow the turn.** `PlaySurface` takes its normal from
  `cross(dv, du)`, which is outward at rest; sweeping the ring radius through zero to its
  negative reverses the parametrisation's handedness, so past the halfway point that vector
  points *into* the shape and the pieces are left standing inside it, hidden and unplayable.
  The normal has to be chosen by which side is out of the shape now, not by the formula's
  sign - and the piece, the seat and the patch's thickness all read it.
- Esc/`G` still leave the view; inverting is not a mode of its own.

**Tests.** No piece's foot is inside the surface at any `evert` in [0, 1] (sample it: the
seat plus half a thickness along the normal is further from the local tube axis than the
seat is). The button's two states capture: `--evert 0` and `--evert 1` both render
validation-clean with every piece visible from outside.

**Acceptance.** Press INVERT on `torus`: the hole closes, reopens inside out, and the game
is still playable with every piece on the outside. Press it again and the board comes back.

### M17.8 The drag axes are the wrong way round

**Want.** Middle-drag left and right should move the board the way the *other* axis does
now, and up and down likewise: the two are swapped.

**Build.** One swap in `src/gui/main.cpp`'s motion handler - `xrel` drives
`geometrySlideV` and `yrel` drives `geometrySlideU` - and the `slidesAlongRanks` gate moves
with it, so the axis that is not glued is the one that stays still.

**Acceptance.** On `torus`, dragging left and right carries a1 along the ranks; on
`cylinder`, which glues only the files, left and right does nothing and up and down slides.

### M17.9 A full lap, and not a two-cell one

**Want.** Sliding the board should carry a1 to b1, to c1, all the way round and home.

**Have.** It comes home after *two cells* and snaps. The cause is known and is one line:
`wrapSlide` in `src/render/play_surface.cpp` is `fmod(s, 2.0f)` applied to a slide measured
in **cells**, while the warps repeat after two *laps* - `2 * nx` cells along the files and
`2 * nz` along the ranks. (One lap for the orientable surfaces; two where a seam reverses a
coordinate, which is why the window is two.) `Settings` normalises with `fmod(…, 16.0f)`,
which is two laps only for an eight-wide board; the wrap belongs in one place, in cells,
derived from the board's own extent.

**Tests.** The existing "a whole lap brings the board home" case passes today *for the
wrong reason* - `fmod(16, 2)` is 0, so it compares the board against itself - and must be
rewritten: a lap of `2 * nx` cells reproduces the unslid board, **and** a half lap does
not, and every intermediate slide moves every seat by less than a cell.

**Acceptance.** Hold the middle button and drag: a1 walks the whole file and arrives back
at a1, having crossed the seam once (twice, mirrored, on `mobius`).

### M17.10 Semi-transparent squares

**Want.** A button that makes the board translucent, so the far side of the shape and the
pieces standing on it can be seen through the near side.

**Build.**

- A `Settings::geometryGhost` (an alpha, 0.35 to 1) and a `GHOST` button beside `SHAPE`.
  The alpha rides in `MeshVertex::color.a`, which already exists and costs nothing.
- **Blending is off in the board pipeline today** (`blendEnable` is left false in all three
  pipelines in `src/render/board_renderer.cpp`). The surface mesh needs a second pipeline -
  same shaders, same vertex input - with `blendEnable`, src-alpha / one-minus-src-alpha,
  and `depthWriteEnable = false`; the opaque pass is unchanged.
- Order: pieces and everything else opaque first, then the translucent board with the depth
  test on and depth writes off. No per-triangle sort: the error that leaves is between two
  sheets of the same translucent board, which reads as what it is.
- The pieces stay opaque. The point is to see them.

**Tests.** A validation-clean GPU render at a ghosted alpha whose luminance variance is
*higher* than the opaque one (the far side now contributes). `geometryGhost = 1` is
bit-identical to today. The flat board never takes the blended pipeline.

**Acceptance.** On `torus` with GHOST on, the pieces on the far side of the ring are
visible through the near side, and a click still selects the near cell.

### M17.11 Centre the shape

**Want.** `torus` and `klein` sit off-centre in the window.

**Have.** Unknown which of three it is, and the first job is to measure rather than guess -
project `PlaySurface::bounds()`'s centre with the effective camera and compare it with the
board rectangle's centre. The suspects, in order:

1. `Session::setBoardAspect` re-frames on `view::boundsOf(placements_)` - the **flat**
   layout - and only keeps the previous target because `framedOnce_` is set. A target
   computed for one box and a distance computed for another do not centre anything.
2. In the capture path `frameBoard` runs before the real board aspect is known, so the
   framing it does is for a square window.
3. `PlaySurface::bounds()` pads by a constant 0.9 on every side, which is symmetric in the
   world but not on screen once perspective is applied.

**Build.** While the geometry view is on, the surface's own bounds are the only thing the
camera is ever framed on - on entry, on resize, and on a variant change - and the pad is
applied where it belongs, which is to the shape's extent and not to the camera's target.

**Tests.** For each of the four shapes, project the bounds' centre through the effective
camera and assert it lands within 2% of the board rectangle's centre; and the same after a
resize.

**Acceptance.** Every shape opens centred, and stays centred when the window is resized.

### M17.12 The shapes above two dimensions

**Want.** `torus3d`, `hyper4` and `t6` get shapes of their own: nested shells, a hypercube,
and the quintic.

**Have.** `hasPlaySurface` is true only for a glued *two*-dimensional board, and M17.4
deferred the rest on the argument that above two dimensions the extruded lattice already is
the shape. That argument holds for a board with no gluing; it does not hold for these
three, and the library screen already draws all of them:

- **`torus3d`** - the overture's `tube` with its `shell` parameter: two of the three
  gluings give T², the level axis becomes the radial direction, and the four levels come
  out as four nested shells about one core circle. The third gluing has nowhere in space to
  go, which the overture says out loud and the play board must too.
- **`hyper4`** - a tesseract: a 4-cube projected into three dimensions as two nested cubes
  joined corner to corner, the fourth axis being the nesting.
- **`t6`** - the Calabi-Yau quintic already in `src/render/quintic.hpp`, which the T6
  overture collapses onto. Whether a 6-torus's *adjacency* survives that embedding is the
  open question, and it is the question the work has to answer first: if the quintic cannot
  carry the lattice so that neighbouring cells are neighbours on it, the honest answer is
  to say so in the view rather than to draw a pretty lie.

**Build.** This is the increment that needs a structure rather than a patch.
`PlaySurface` becomes the two-dimensional case of a `PlayShape`: the same three products -
a patch per cell, a seat per cell, and a gapless grid to pick against - produced for any
dimension, keyed off `app::SurfaceKind` extended with `Torus3d`, `Hypercube` and `Quintic`.
Everything above it - the mesh builder, the pose, the picker, the camera framing - is
already written against those three products and should not need to know.

**The invariant that decides whether any of it is honest**: two cells adjacent in the
lattice are adjacent in the drawing, and two cells that are not, are not. A projection that
breaks it is a picture, not a board, and must be labelled as one.

**Tests.** Per shape: every cell has a patch, the patches do not cross, picking returns the
cell under the cursor, and the adjacency invariant above holds for every pair the geometry
glues. A `--geometry` capture per variant, validation-clean.

**Acceptance.** `chessbox_gui torus3d --geometry` plays on four nested shells;
`hyper4 --geometry` on a hypercube; `t6 --geometry` either on the quintic, or on the
extruded lattice with the reason stated in the interface.

### M17.13 `torus3d`/`hyper4` read as a scatter of fish scales, not a shape

The M17.12 first pass shipped and immediately showed two bugs, both diagnosed below by
reading `PlaySurface::buildStacked` (`src/render/play_surface.cpp:254-345`) and confirmed
by printing real seat data (see **Evidence**). `chessbox_gui torus3d --geometry` draws a
fan of thin, wildly-angled slabs with no visible tube or ring; `hyper4 --geometry` draws a
chaotic cloud of small squares with no visible cube-in-a-cube.

**Want.** `torus3d --geometry` reads as four smooth, nested rings; `hyper4 --geometry`
reads as two nested cube-shaped clusters, the way the M17.12 plan intended.

**Have - the bug.** `buildStacked`'s local helper `uprightSite` (lines 261-269) builds the
tangent frame for every cell from its immediate file/rank neighbours, found like this:

```cpp
c.c[0] = static_cast<std::int16_t>(std::clamp(base.c[0] + df, 0, nx - 1));
c.c[1] = static_cast<std::int16_t>(std::clamp(base.c[1] + dr, 0, nz - 1));
```

This **clamps** the neighbour's coordinate at the axis's two ends. That is correct only
for a *bounded* axis (no cell past the edge). `torus3d`'s file and rank axes are declared
`kind = "periodic"` in `variants/torus3d.toml` - there is no edge, a cell at file 0's
"previous" neighbour is file 3 wrapped around, not file 0 again. Clamping instead of
wrapping makes `uprightSite` return the **cell's own position** as its neighbour at every
boundary index, which for a 4-wide periodic axis is every index touching 0 or 3 - most of
the board. The resulting tangent is then a one-sided, half-length, wrongly-based estimate,
and the surface normal (`cross(tangentV, tangentU)`) inherits the error: each affected
cell's local frame tilts in whatever direction that bad tangent happens to point, which is
not continuous from one cell to the next. That is the "fish scales"/fan-of-slabs picture.

`hyper4` has no periodic axes at all (a plain 4-D box - "no identifications", per
`docs/plan/M17.12-shapes-above-two-dimensions.md`), so clamping there is the *correct*
choice of neighbour - there genuinely is no cell past the edge. But the same formula still
mis-sizes the boundary: `cellU = 0.5f * view::length(tangentU)` (line 305) assumes a
**centred, two-sided** difference spanning two cells; at a true boundary only one side is
real and `tangentU` spans one cell, so halving it produces a tile **half the width** it
should be. That is the discontinuity in `hyper4`'s scattered look - every cell on a
boundary face of the tesseract is drawn at roughly half the size of its interior
neighbours, which breaks the fan of squares apart.

**Evidence.** A diagnostic dump of `PlaySurface::build(torus3d).seats()` at level 0, every
`(file, rank)`, shows exactly the signature of the clamp bug - a mirror-symmetric pattern
around the middle of each 4-wide axis instead of the uniform values a fully periodic 4x4
torus must have (every cell is equivalent by the identification group; `torus3d.toml`'s
own comment says so: *"no cell is special"*):

```
f0 r0: stepU=0.889 stepV=5.355      f1 r0: stepU=1.257 stepV=5.355
f0 r1: stepU=0.889 stepV=7.573      f1 r1: stepU=1.257 stepV=7.573
f0 r2: stepU=0.889 stepV=7.573      f1 r2: stepU=1.257 stepV=7.573
f0 r3: stepU=0.889 stepV=5.355      f1 r3: stepU=1.257 stepV=5.355
f2 r*: stepU=1.257 (matches f1)     f3 r*: stepU=0.889 (matches f0)
```

`stepU` takes exactly two values (0.889 at file 0/3, 1.257 at file 1/2) and `stepV` takes
exactly two values (5.355 at rank 0/3, 7.573 at rank 1/2) - a clean f↔(3-f), r↔(3-r)
mirror, which is the clamp substituting the opposite-parity neighbour's own position. A
correctly wrapped periodic axis has no such mirror: every file value is interchangeable
with every other by the torus's own symmetry, so `stepU` must come out the same (within
floating-point/metric tolerance) for all four, and likewise `stepV`.

**Build.** Replace `uprightSite`'s per-axis neighbour lookup with one that treats a
periodic axis as periodic and a bounded axis as bounded, independently for axis 0 (U) and
axis 1 (V):

- **Is the axis periodic?** `v.geom.boundaryKind(axis, Side::Max) == BoundaryKind::Periodic`
  - the same check `app::overtureSignature` already uses (`src/app/overture.cpp:71`,
  `g.boundaryKind(a, Side::Max) == BoundaryKind::Periodic`). `Side` and `BoundaryKind` are
  declared directly in namespace `cb` (`src/geometry/geometry.hpp`).
- **Periodic axis:** wrap, never clamp - `(idx + offset + extent) % extent` for both
  `offset = +1` and `offset = -1`. The existing central-difference formula
  (`tangent = p_plus - p_minus`, `cellSize = 0.5f * length(tangent)`,
  `ex = normalize(tangent)`) is otherwise unchanged and is now correct for every cell,
  boundary or not, because there is no boundary.
- **Bounded axis, interior cell** (`0 < idx < extent - 1`): unchanged - the existing
  central difference is already correct here.
- **Bounded axis, boundary cell** (`idx == 0` or `idx == extent - 1`): only one real
  neighbour exists. Use a one-sided difference **scaled to one cell**, not a clamped
  two-sided one scaled to a (fictitious) two cells:
  ```cpp
  // idx == 0: only the +1 side is real.
  tangent = p_plus - centre;       // NOT p_plus - centre_clamped_to_self
  cellSize = view::length(tangent);  // NOT 0.5f * length(...)
  exDir = view::normalize(tangent);
  // idx == extent - 1: only the -1 side is real, symmetric construction.
  tangent = centre - p_minus;
  cellSize = view::length(tangent);
  exDir = view::normalize(tangent);
  ```
  `centre` is already computed in `buildStacked`'s outer loop (line 294) before
  `uprightSite` is called, so it is available to pass in or capture.
- Apply this choice **independently per axis** (U from axis 0, V from axis 1): a future
  authored shape could glue one in-plane axis and not the other, and the code should not
  assume both behave the same way. `torus3d` exercises the periodic branch on both axes;
  `hyper4` exercises the bounded-boundary branch on both axes; nothing today exercises a
  mix, but the code must not rule it out.
- The `normal`, `ex`, `ey`, `quat`, and the patch/quad construction that follow are
  unchanged - they already consume `tangentU`/`tangentV`/`cellU`/`cellV` generically.

**Tests.**

- `tests/render/test_play_surface.cpp`, strengthen the existing
  `"a three- and four-dimensional variant plays on its own shape"` case (currently only
  checks `stepU/stepV > 0.01f`, which is why this bug shipped): for `torus3d`, assert every
  seat's `stepU` is within a small tolerance (a few percent) of every other seat's `stepU`,
  and likewise for `stepV` - citing the variant's own "no cell is special" claim as the
  justification. This directly fails today (0.889 vs 1.257, a 41% spread) and must pass
  after the fix.
- New case for `hyper4`: for a cell one step in from a true boundary (e.g. file index 1)
  and the boundary cell next to it (file index 0), `stepU` must be within roughly 20% of
  each other - not the ~40% systematic drop the halved one-sided formula produces today.
  Check this on all four axes (file, rank, level, aeon are all bounded on `hyper4`; pick
  one representative boundary pair per axis).
- Keep the existing patch/picking tests passing unchanged - the patch and pick-ray
  construction do not change, only the tangent inputs they are built from.

**Acceptance.** `chessbox_gui torus3d --shot t.ppm --geometry` shows four continuous,
smoothly nested rings with no isolated radiating slabs; `chessbox_gui hyper4 --shot h.ppm
--geometry` shows two legible, roughly cube-shaped clusters of squares rather than a
scattered fan. Both stay validation-clean.

### M17.14 The camera does not follow a variant switch while already in shape mode

**Want.** Loading a different variant - or the same one again, "start over" from the
library screen - while the geometry view is already on must centre the camera on the *new*
variant's shape. It must not leave the camera framed on the previous variant's shape, or
on the new variant's flat board.

**Have - the bug.** `Settings::geometryView` (and `geometrySlideU/V`, `geometryEvert`,
`geometryInvert`) live on `Shell::settings_`, a single member that `Shell::startGame`
(`src/app/shell.cpp:208-`) never resets - it only copies specific fields
(`flatView`, `hotSeat`, `confirmMoves`, `cameraMode`, `followStrength`, `theme`) onto the
freshly-built `Session`. `geometryView` itself is not one of them, so it is **sticky**
across a variant change: if it was on for variant A, it is still on for variant B.

`Session::create` (`src/app/session.cpp`) frames its initial camera from the **flat**
layout's bounds unconditionally - `Session` does not know `PlaySurface` exists.

The only place that ever frames the camera on the *shape's* bounds is `frameBoard`
(`src/gui/main.cpp:198-217`), called from exactly one place in the main loop
(`src/gui/main.cpp:941-953`):

```cpp
const bool nowSurface = optionsFor(*shell).surface;
if (nowSurface != wasSurface) {
  renderer->setOptions(optionsFor(*shell));
  frameBoard(*shell);
  wasSurface = nowSurface;
}
```

This fires only when `nowSurface` (purely a function of `geometryView` and the *current*
variant's `hasPlaySurface`) differs from last frame's value. It says nothing about which
variant is loaded. So: player turns SHAPE on for `torus` (`nowSurface` flips false→true,
`frameBoard` fires, correctly centred); player then opens New Game and picks `torus3d`
without turning SHAPE off. `request.loadVariant` fires
(`src/gui/main.cpp:928-931`), `startGame` builds a brand-new `Session` for `torus3d`,
freshly flat-framed by its constructor - but `geometryView` was already `true` and still
is, so `nowSurface` is `true` both before and after this reload. The toggle check sees no
change and never calls `frameBoard`. The camera stays on `torus3d`'s flat-board framing:
"the window is centred at the centre of the flat board, not the centre of the shape."

**Build.**

- Extract the policy `frameBoard` currently implements into a plain, headless-testable
  function in the render layer (which already depends on `app`, see `render/ui.hpp`'s
  `#include "app/session.hpp"` - this is not a new layer dependency), alongside
  `PlaySurface` since it is the thing being framed on:
  ```cpp
  // src/render/play_surface.hpp (or a small new pair if preferred - this is one
  // function and does not need its own module)
  namespace cb::render {
  /// Frame `session`'s camera on whatever the geometry view would show right now: the
  /// play surface's own bounds, headroom 0 (M17.11), while `options.surface` is set;
  /// the flat layout's bounds otherwise. Reads nothing from `session` but its variant
  /// and placements, so it is correct - and cheap - to call unconditionally every time
  /// anything that could change what is drawn has changed: the surface toggle, the
  /// pose, or the variant itself. The caller never has to work out which of the three
  /// actually happened (M17.14).
  void frameGeometryCamera(app::Session& session, const BoardOptions& options,
                           SurfacePose pose);
  }
  ```
  Its body is `frameBoard`'s current one, verbatim (the `surf.empty()` fallback to flat
  framing included).
- `main.cpp`'s `frameBoard` becomes a thin wrapper: `render::frameGeometryCamera(*session,
  optionsFor(shell), poseFrom(shell.settings()))`. No behaviour change there.
- In the `request.loadVariant` block (`src/gui/main.cpp:928-931`), call `frameBoard(*shell)`
  **unconditionally** right after `renderer->setOptions(optionsFor(*shell))`, and set
  `wasSurface = optionsFor(*shell).surface` there too, so the toggle check later in the
  same tick does not redundantly re-fire. This is always correct (per the function's own
  doc comment above) and costs one extra `PlaySurface::build` on a variant load only - the
  same cost `frameBoard` already pays every time the toggle flips, which the project has
  already measured as microseconds (ADR-0019's "Costs" section).

**Tests.** This is the first part of the surface-view policy that can be headless-tested
directly, because it no longer lives only in `main()`:

```cpp
// tests/render/test_play_surface.cpp
TEST_CASE("the camera reframes on the shape when the variant changes under it",
          "[render]") {
  // The regression this pins: geometryView is sticky across Shell::startGame (never
  // reset), so a one-shot "did the toggle flip" check misses a variant switch that
  // happens while it was already on - the camera is left on the wrong shape, or on the
  // new variant's flat board.
  auto open = [](const char* name) {
    auto s = app::Session::create(test::loadVariant(name));
    REQUIRE(s.has_value());
    return std::move(*s);
  };
  BoardOptions opts;
  opts.surface = true;

  std::unique_ptr<app::Session> session = open("torus");
  frameGeometryCamera(*session, opts, SurfacePose{});

  session = open("torus3d");  // switched variant; opts.surface is still true throughout
  frameGeometryCamera(*session, opts, SurfacePose{});

  const view::Bounds want = PlaySurface::build(test::loadVariant("torus3d")).bounds();
  const view::Vec3 target = session->camera().target;
  CHECK_THAT(target.x, WithinAbs(want.centerX(), 0.05f));
  CHECK_THAT(target.y, WithinAbs(want.centerY(), 0.05f));
  CHECK_THAT(target.z, WithinAbs(want.centerZ(), 0.05f));
}
```

This fails today for the obvious reason (nothing in this sequence ever calls the framing
function for the second session) and passes once `frameGeometryCamera` exists and the
`loadVariant` call site calls it unconditionally, matching what the real fix does.

**Acceptance.** Interactively (no capture flag can reach this - it needs two loads in one
process; note this limitation next to ADR-0018's similar one for the present path): launch
the game, load `torus`, press SHAPE (correctly centres, unchanged). Open the pause menu's
New Game screen and pick `torus3d` *without* turning SHAPE off first - the moment it loads,
`torus3d`'s own shape is centred in the window. Repeat starting from `klein` into `hyper4`,
and picking the *same* shaped variant again (the "start over" case). None of them should
ever show the previous variant's framing or the new variant's flat-board framing.

### M17.15 A move on the shape animates instead of teleporting

**Want.** A piece crossing the shape travels there - a knight arcs directly from its
square to its landing square; everything else (rook, bishop, queen, king, pawn) visibly
passes through every square the engine's own route says it passes through, the way M11
already draws a move on the flat board.

**Have.** `BoardRenderer::buildInstances`'s surface branch draws every piece at its
*destination* seat every frame, unconditionally - `board_renderer.cpp:772-799` loops
`surf.seats()` and places each occupied cell's piece there from `p.at(seat.cell)`, which
the engine has already updated the instant the move was applied. The animation parameter
is received and discarded: `(void)anim;  // the warped route is M17's remaining piece; a
mover shows at its seat` (`board_renderer.cpp:800`). There is no skip for the cell the
piece is travelling *to* either (the flat path has one -
`if (anim != nullptr && anim->active() && pl.cell == anim->travellingTo()) continue;`,
`board_renderer.cpp:939` - the surface loop has no equivalent), so once a mover is drawn
it would simply sit correctly at its landing square with nothing animating towards it:
a teleport.

The flat board's existing machinery - `view::MoveAnimation`, `view::tracePath`,
`view::routeRuns` - already carries everything needed to know the route (`MovePath.steps`,
each tagged `Interior`/`Portal`/`Bounce`; `MovePath.leap` for a knight/hop) and the timing
(`MoveAnimation::advance`/`progress`/`setProgress`, already driven by `Session` and already
reaching `--move-t` in captures). None of it assumes a flat board in its *logic* - M11's
whole premise (AGENTS.md: "the move camera reads the trace, never the topology") - but
`MoveAnimation::sample()` sites its `lift` along world **Z** (right for a flat board,
wrong on a curved one, where "up" is the local surface normal and world Z is often not
that), and its portal visuals assume two glued edges are drawn far apart on screen, which
is never true on the surface - a glued seam there is drawn as one continuous place, so
there is nothing to open a doorway between.

**Build.**

- **Keep `MoveAnimation`'s timing, drop its flat-specific sampling for this path.**
  `Session` already owns one `view::MoveAnimation` and drives it the same way regardless of
  view mode - nothing here changes that. Add one small, safe accessor so the renderer can
  read what move is actually in flight, since `MoveAnimation::sample()`'s own x/y/z/lift
  stay correct (and unchanged) for the flat board:
  ```cpp
  // src/view/move_anim.hpp, inside class MoveAnimation
  /// The route this animation was last started with. Needed by anything that samples a
  /// *different* placement of the same move - the geometry view's surface sampler, which
  /// cannot reconstruct the route itself (board_renderer.cpp has no access to
  /// `Session::lastPath_`, only to this animation) (M17.15).
  [[nodiscard]] const MovePath& path() const noexcept { return path_; }
  private: MovePath path_;  // set in start(), alongside the existing leap_/to_/etc.
  ```
- **A pure sampler, parallel to `moveCamera`, in the render layer** (`PlaySurface` already
  lives there and already depends on `app`; this needs nothing new in the dependency
  graph). Add to `src/render/play_surface.hpp`/`.cpp`:
  ```cpp
  struct SurfaceMoveSample {
    view::Vec3 position{};
    view::Vec3 normal{0.0f, 0.0f, 1.0f};
    std::array<float, 4> quat{{0.0f, 0.0f, 0.0f, 1.0f}};
    float fit{1.0f};  ///< the piece scale factor seats already carry (board_renderer.cpp:789)
  };
  /// Where the travelling piece sits on `surf` at progress `t`, and which way it stands.
  /// Pure in `t`, like `moveCamera` and like an overture - nothing here reads a clock.
  [[nodiscard]] SurfaceMoveSample surfaceMoveSample(const view::MovePath& path,
                                                     const PlaySurface& surf, float t);
  ```
  Its body:
  - Look up each `SurfaceSeat` the route touches by cell (same linear scan
    `view::routeRuns` already does over a handful of cells - a move touches a handful
    regardless of board size).
  - **Leap** (`path.leap`): arc directly from `path.from`'s seat to `path.to`'s seat -
    `position = mix(fromSeat.centre, toSeat.centre, t)` lifted outward along
    `normalize(mix(fromSeat.normal, toSeat.normal, t))` by the same
    `sin(t * pi) * 0.65f` the flat board's leap already uses
    (`move_anim.cpp:488`) - peaking mid-flight, zero at both ends. `normal`/`quat` blend
    (`quatOf`-style, reusing the private helper already in `play_surface.cpp`) between the
    two seats' frames.
  - **Glide** (not a leap): walk `path.from` then every `PathStep.to` in order - this *is*
    "every square between start and end", directly off the route the engine already
    computed, with no reinterpretation. Build the ordered list of seat centres, then use
    the same arc-length parametrisation `move_camera.cpp`'s file-local `pointAlong` already
    implements (move it out of that file's anonymous namespace and export it from
    `move_camera.hpp` as `view::pointAlong(points, s)`, generalised to a plain point list
    rather than a `RouteRun`, so both call sites share one definition rather than drifting
    apart - the camera and the piece disagreeing about where "the move" currently is would
    be exactly the class of bug ADR-0011's invariant exists to rule out). **A `Portal` or
    `Bounce` step is not a special case here** - unlike the flat board, there is no gap to
    open a doorway across: the seams the step crosses are already drawn as one continuous
    region of the surface, so the walk simply continues through the point sequence with no
    cut. (`Bounce` cannot occur in practice - `hasPlaySurface` already excludes
    `MirrorBox`.) Orientation snaps to the nearest route cell's seat quat as the piece
    passes it; a slerp between consecutive quats would read more smoothly and is a
    reasonable follow-up, not required here.
  - `fit`: blend the two relevant seats' `stepU*stepV`-derived fit the same way position
    blends, so a piece does not visibly resize in a jump at each cell boundary.
- **`BoardRenderer::buildInstances`'s surface branch** (around `board_renderer.cpp:772`):
  add the destination skip the flat path already has -
  `if (anim != nullptr && anim->active() && seat.cell == anim->travellingTo()) continue;`
  inside the `surf.seats()` loop - and after it, where `(void)anim;` is today, emit the
  travelling piece: `const Piece moving = p.at(anim->travellingTo());` (the engine has
  already moved it there, exactly as the flat path's equivalent line reads
  `board_renderer.cpp:1236`), and if not empty, `const SurfaceMoveSample s =
  surfaceMoveSample(anim->path(), surf, anim->progress());`, then build an `Instance` the
  same way the static loop does (`body.center`/`body.quat`/`body.scale`) but sourced from
  `s` instead of a `SurfaceSeat`.

**Tests.** New cases in `tests/render/test_play_surface.cpp` (pure geometry, no GPU):

- A leap's sample at `t = 0` equals `fromSeat.centre` and at `t = 1` equals `toSeat.centre`
  (within float tolerance); at `t = 0.5` it is displaced *outward* along the blended
  normal relative to the straight chord between the two - i.e. it visibly arcs rather than
  cutting through the shape.
- A glide's sample at `t = 0` and `t = 1` match the route's first and last seat; sampled
  across a dense sweep of `t`, the position visits a neighbourhood of every intermediate
  `PathStep.to` seat in order (monotonically increasing arc-length), and never departs the
  surface by more than the leap's own lift bound (it should hug the surface, not arc).
- A glide across a seam (pick a scripted rook move on `torus` that crosses the glued file
  edge) produces no discontinuity in the sampled position around the step that crosses it
  - the point just before and just after the crossing are close together (within one
  cell's `stepU`), unlike the flat board's portal cut.
- `BoardRenderer::buildInstances`, GPU: a validation-clean render mid-move
  (`anim.setProgress(0.5f)`) on `torus` shows the moving piece displaced from both its
  start and end seats, and neither the start nor end seat double-draws it.

**Acceptance.** `chessbox_gui torus --script "click a1\nclick a2" --geometry --move-t 0.5
--shot mid.ppm` shows the pawn part-way along the surface between a1 and a2, not sitting
statically at a2. A scripted knight move shows a visible arc clear of the surface at
`--move-t 0.5`. `--geometry` renders without the flag are unaffected (progress defaults to
1, the sampler's own end-state, identical to today's static placement).

### M17.16 The move camera follows on the shape

**Want.** The existing camera-follow setting (off / follow the piece / follow the route)
works in the geometry view too: "off" is the player's own orbit, unchanged - the ordinary
third-person view, as now; "piece"/"route" put the camera behind the moving piece as it
crosses the shape, the same way it already does on the flat board.

**Have.** `Settings::cameraMode` (`"off"`/`"piece"`/`"route"`) already drives
`Session::setCameraMode`, which sets `cameraPolicy_.follow` to a `view::FollowMode`; the
cycle button for it already lives in the settings menu (`ui_menus.cpp:858-867`) and is not
gated on `geometryView` - it is simply **inert** there, because `Session::camera()`
(`session.cpp:253-276`) blends the move-camera shot using `placements_`/`viewCfg_` - the
*flat* layout, always, regardless of what `BoardOptions::surface` the renderer is actually
drawing. `view::moveCamera`/`view::routeRuns` themselves need no change for this: they are
already geometry-agnostic, reading only a `MovePath` and a `std::vector<Placement>`
(AGENTS.md's own description of M11). The gap is entirely that nothing ever builds a
*surface* `Placement` list and asks for the blend against it.

Two more things follow from that gap once it is closed: `moveCamera`'s "look along travel"
direction (`move_camera.cpp:188-191`, `worldDir(cfg, run.dir)`) projects a lattice
direction through `ViewConfig::screenAxes` - a flat-layout concept with no surface
equivalent - but `moveCamera` already falls back to the run's own 3-D chord
(`move_camera.cpp:192`, `dir = normalize(last - first)`) whenever `worldDir` returns
near-zero, which is exactly what happens when `cfg.screenAxes` is empty. And picking on the
surface (`src/gui/main.cpp`, the `optionsFor(*shell).surface` click branch) currently calls
`session->camera()` directly with no `shotInFlight()` guard - harmless while nothing ever
animated a surface move, not harmless once M17.15 and this both exist: a click mid-shot
would now pick against a camera that is about to move, which is exactly the desync
ADR-0011's invariant exists to prevent, and which `Session::clickPixel` already guards
against for the flat board (`session.cpp:636`, `if (shotInFlight()) return kInvalidCell;`).

**Build.**

- **One function, not two copies of the blend.** `Session::camera()`'s body (past the
  `pullBack_` line) is the move-camera blend, parametrised internally on `placements_` and
  `view::boundsOf(placements_)`. Split it:
  ```cpp
  // session.hpp
  /// `camera()`'s own blend, against an arbitrary placement set and its bounds instead of
  /// the session's flat layout - what the geometry view passes, built from
  /// `render::PlaySurface::seats()` (M17.16). One definition either way, so the move
  /// camera cannot say something different from what `camera()` already promises: see the
  /// note on `camera()` for why the blend is folded into one accessor at all.
  [[nodiscard]] view::OrbitCamera cameraOver(const std::vector<view::Placement>& placements,
                                              const view::Bounds& scene) const noexcept;
  ```
  `camera()` becomes `return cameraOver(placements_, view::boundsOf(placements_));` after
  its existing `pullBack_`/early-return lines (those stay exactly where they are - this is
  a pure extraction, not a behaviour change, and the existing camera/move-camera tests must
  keep passing unchanged).
- **Build the surface placements once per frame**, in `src/gui/main.cpp`, next to where
  `optionsFor`/`poseFrom` already live:
  ```cpp
  std::vector<view::Placement> surfacePlacements(const PlaySurface& surf) {
    std::vector<view::Placement> out;
    out.reserve(surf.seats().size());
    for (const SurfaceSeat& s : surf.seats())
      out.push_back({s.cell, s.centre.x, s.centre.y, s.centre.z, 0});
    return out;
  }
  ```
  Wherever the loop currently calls `session->camera()` while
  `optionsFor(*shell).surface` is true (the render call and the picking call both need the
  *same* one, per ADR-0011), call `session->cameraOver(surfacePlacements(surf),
  surf.bounds())` instead, with `surf` the same `PlaySurface::build(variant, pose)` already
  being built this frame for drawing - do not build it twice.
- **`ViewConfig` for the surface blend is the default, empty one** (`view::ViewConfig{}`)
  wherever `cameraOver`/`moveCamera`'s `cfg` parameter is threaded through - its only
  consumers (`worldDir`, `travelsGridAxis`) both degrade to their already-correct
  fallbacks on an empty config (chord direction; never "grid axis", since the surface view
  has no grid-axis concept). No change to `move_camera.cpp` is needed for this.
- **Gate surface picking on `shotInFlight()`**, matching `clickPixel`'s own rule: in the
  surface click branch in `main.cpp`, skip (or no-op) the pick when
  `shell->session()->shotInFlight()` is true, exactly as the flat board already refuses a
  click mid-shot.
- `Settings::cameraMode` needs no new value and no new UI - the existing three-state cycle
  already reaches `Session::setCameraMode`, which this reuses unmodified. `FollowMode::Off`
  is already "the player's own orbit, unaffected" (`moveCamera` returns the settled framing
  immediately); that is literally "3rd person, as now" and needs no code here at all.

**Tests.**

- `tests/render/test_play_surface.cpp` (or a sibling), headless: construct a `Session` on
  `torus`, start a move, call `cameraOver(surfacePlacements, surf.bounds())` at a few
  values of `anim.progress()` with `cameraPolicy_` set to `Piece`/`Route` - note this needs
  the same small accessor path M17.14 already asked for (or `cameraPolicy_`/`lastPath_`
  exposed some other way; either spec's refactor satisfies the other) - and assert the
  returned camera's `target` tracks the route (moves monotonically along it) rather than
  sitting fixed at the settled framing `FollowMode::Off` would give.
- A regression test that `FollowMode::Off` against surface placements is bit-identical to
  `frameGeometryCamera`'s own settled framing (M17.14) - the strict no-op the whole feature
  promises when the player has not opted in.
- A picking test: with a shot in flight (`anim.setProgress(0.4f)`, `cameraPolicy_` set to
  follow), the surface pick call returns `kInvalidCell` rather than resolving against a
  stale camera.

**Acceptance.** Set "follow the route" in settings, make a long sliding move on `torus` in
the geometry view: the camera leads the piece around the ring, pulling back as it travels,
and settles back to the player's own orbit once the move completes - the same character the
flat board's follow already has, now on the shape. Set it back to "off": nothing about the
geometry view's camera changes from how it behaves today.

### M17.17 Align the shape so the moving piece is never on the far side

**Want.** An option that, while the camera is following a move, keeps the piece on the
side of the shape the camera can actually see without looking through the shape's own
geometry to find it - concretely, if a piece travels along a torus's inner ring (the side
facing the hole), the torus turns so that ring becomes the outer one before or as the
camera gets there, rather than the camera ending up inside the donut looking out through
the tube wall.

**Have.** Nothing - this is new. But the mechanism already exists in a different guise:
`SurfacePose::slideU`/`slideV` (ADR-0019) already rotate *which part of the surface's own
parametrisation* sits where - the middle-mouse-button slide is a player manually doing
exactly this. "Align" is the same operation, chosen automatically rather than dragged, and
only for the file axis (`slideU`) in the first cut: on every shape in the catalogue, `u`
is the angle that wraps the ring a torus's inner/outer side sits on (`tube`'s `th`
parameter in `overture_scene.cpp`), while `v` (the ones that would need `slidesAlongRanks`)
governs the cross-section twist, not which side faces the camera. Scoping to `slideU` on
the surfaces that already support sliding (`PlaySurface::slidesAlongRanks` names which
variants slide at all - `torus`/`klein`; `cylinder`/`mobius` have no inner/outer ring to
begin with, since they do not close into one) keeps this from needing new theory about the
D >= 3 stacked shapes, which do not support slide or invert yet (M17.12's stated limits).

**Build.**

- **A facing measure already sits on every seat.** `SurfaceSeat::normal` is the outward
  direction `PlaySurface` already computes; "does the camera clip through the shape to see
  this cell" is well approximated by whether that normal points *away* from the camera:
  `facing(seat, camera) = dot(seat.normal, normalize(camera.eye() - seat.centre))`. A
  seat with `facing <= 0` is on the far side of the shape as the camera currently sits -
  not necessarily occluded by a precise raycast, but a cheap, honest proxy consistent with
  what the renderer already uses for shading and picking.
- **The search.** Given the cell the camera is about to follow (the destination of the
  move about to start, or the currently-nearest route cell while one is in flight) and the
  *settled* camera (`Session::cameraOver` with `FollowMode::Off` - the framing the shot
  would otherwise lead from), sample `facing` at a handful of candidate `slideU` offsets
  (e.g. 16, evenly spaced across the shape's own period -
  `PlaySurface`'s existing `wrapSlide(s, period)` helper and its `2 * nx` period
  (`play_surface.cpp:84`, used at `play_surface.cpp:131-132`) - by
  rebuilding just that one seat's position at each candidate (no need to rebuild the whole
  `PlaySurface` per candidate - add a small helper that samples a single seat's
  centre/normal at a given `(cell, pose)`, factored out of `PlaySurface::build`'s per-cell
  body) and keep the offset with the largest `facing`. This is a search, not a closed
  form - the embeddings have no general inverse for "which slide makes this point face
  outward," and a 16-point sweep is cheap (the same order of cost ADR-0019 already
  accepted for rebuilding the whole surface every frame).
- **Applying it.** `Settings::geometryAlign` (bool, default `false` - another strict
  no-op by default, consistent with every other M17 setting). An `ALIGN` button in the
  rail, shown next to `SHAPE` only when `cameraMode != "off"` (aligning has nothing to do
  while the player is not following a move - same reasoning `INVERT`/`GHOST` already use
  for when they are offered). While it is on **and** a shot is in flight
  (`shotInFlight()`), ease an *additional* offset into `Settings::geometrySlideU` - on top
  of whatever the player has manually slid to, not replacing it - towards the searched
  value, over the same kind of ramp `INVERT`'s target already uses
  (`main.cpp:904-913`, `dt * 2.0f`-ish), and ease it back to zero once the shot ends. The
  search itself only needs to run once per move (when the shot starts, or when the
  followed cell changes - a slide search every frame would be wasted work for a value that
  only needs to change when the target cell does).
- **Scope.** Offered only where `PlaySurface::slidesAlongRanks` or the plain
  `hasPlaySurface` 2-D case with a closed `u` axis applies - concretely, `torus` and
  `klein` (closed rings with an inner/outer side); `cylinder` and `mobius` never show the
  button (nothing to align - a `u`-slide there just spins an open tube/strip with no side
  that is more "inside" than another). `torus3d`/`hyper4`/`t6` are out of scope for this
  pass, matching M17.12's own stated limits on slide/invert there.

**Tests.**

- `tests/render/test_play_surface.cpp`: for a torus, pick a cell known to sit on the inner
  ring at `slideU = 0` (one exists - the torus's `open` parameter in `tube()` pulls the
  hole open specifically so an inner ring exists to test against) and a settled camera
  framed on the whole shape; assert `facing(seat, camera) < 0` there (the premise the
  feature exists to fix), then assert the search finds a `slideU` for which
  `facing(seat', camera) > facing(seat, camera)` and is positive - the align genuinely
  improves the measure it targets, for the specific case the feature was asked for.
- A continuity check: the chosen offset as a function of a sweep of candidate followed
  cells does not jump discontinuously between adjacent cells (the search should prefer the
  candidate nearest the previous frame's choice when two offsets tie on `facing`, or the
  shape would visibly snap between two rotations for a route that crosses the tie-break
  boundary).

**Acceptance.** Turn ALIGN on, set "follow the route", script a rook's slide along a
torus's inner ring. Without ALIGN, the camera at some point along that route ends up
looking through the tube wall (self-occlusion visible in a capture). With ALIGN on, the
torus itself turns so the travelled ring presents its outer face to the camera and the
piece stays visible throughout.

### M17.18 Klein's normal field flips mid-board, not at its seam

**Want.** The Klein bottle reads as one continuous shape while sliding through it (the
middle-drag that moves `slideU`), the way `torus`/`cylinder`/`mobius` already do. Today it
looks like two shapes glued together: on one side of a line through the middle of the
board the squares (and the pieces standing on them) read as facing/leaning one way, on the
other side the opposite way, and that line sweeps across the board as the slide changes -
confirmed below, it is not an impression, the normal field genuinely reverses there.

**Have - confirmed by measurement, not just by eye.** `PlaySurface::build`'s Klein branch
computes "outward" in two places, both independently, both from the *same* unstable test,
and both away from the pinch where it is actually unstable:

- **The fine mesh shading.** `nrm(u, vv)` (`play_surface.cpp:171-176`) takes the raw
  `normalAt(...)` and flips it to point away from `axisRef(vv)` - the per-rank
  cross-section centroid, averaged over the full gapless grid (`play_surface.cpp:148-161`,
  comment at `148-154`: *"'Outward' at a point is the side away from it"*). This feeds
  every `patch.normal[k]` (`play_surface.cpp:193`) and every `seat.normal`
  (`play_surface.cpp:203`).
- **The per-cell piece orientation.** `ex` (`play_surface.cpp:216-220`) comes from a
  *separate*, coarser tangent - the chord across the whole cell,
  `along = at(u + 0.5/fnx, vv) - at(u - 0.5/fnx, vv)` - projected flat against
  `seat.normal` and normalised. It inherits whatever `seat.normal` already decided, and
  adds its own instability on top (below).

A probe of `PlaySurface::build(klein).patches()`, dumping `patch.normal[]` across one rank
at fine (`kSubdiv = 4`) resolution, shows the fault precisely - two places where
*consecutive fine samples, a quarter-cell apart, inside what should be one smooth square*,
are nearly exact opposites (`slideU = 2.0`, rank 0, patch for file 2, its own four
corners):

```
file 2, corner i=2: n=(0.208, 0.311, -0.927)
file 2, corner i=3: n=(-0.227, -0.472, 0.852)   <- dot with i=2 is -0.98: ~168 degrees
...
file 5, corner i=1: n=(0.607, 0.425, 0.672)
file 5, corner i=2: n=(-0.556, -0.361, -0.748)  <- dot with i=1 is -0.99: ~174 degrees
```

Both loci sit at a *fixed* `u`, independent of rank, and both move together as `slideU`
changes (confirmed: at `slideU = 0` the same two flips sit at files 0 and 4; at
`slideU = 2` they sit at files 2 and 6 - a clean two-cell shift, matching the slide
exactly). That `u` is where `kleinSurf`'s lemniscate cross-section passes through its own
centre - `cr = rho*sin(a(u))` and `ca = rho*sin(2a(u))` are both zero at `u ≈ 0` and
`u ≈ 0.5` (AGENTS.md's own gotcha already names this point: *"on a Klein bottle's crossing
the tangent at the seat and the direction of the next square are most of a right angle
apart"* - this is that crossing, and it is worse than a right angle apart). Exactly there,
the sampled point sits almost *on* `axisRef`'s centroid (the centroid of a figure-eight is
near its own crossing), so `dot(n, out)` in `nrm()`'s flip test is dividing by
almost nothing - the sign it returns is numerical noise, and it comes out different sides
of zero for two points a quarter of a cell apart. The coarser `ex` computation
(`along` spanning a *whole* cell) is the same instability at a coarser scale - its
`seat.normal` is the one that just flipped, and `along` itself is a wide secant straight
across the same crossing.

**Why this cannot simply be "fixed" to zero seams.** The Klein bottle is non-orientable -
that is the entire content of the variant (its own description: *"a bishop that finally
escapes its colour"* because no global two-colouring, equivalently no global consistent
"outward", exists). A continuous outward-pointing normal field, walked all the way around
the closed `u` loop, is mathematically guaranteed to come back reversed from where it
started - this is not a bug to eliminate, it is the shape. The bug is only that the one
unavoidable discontinuity a non-orientable field must have is landing **twice, in the
middle of the board, at a numerically unstable point** instead of **once, at the board's
own declared seam** - the file edge the variant already glues (`klein.toml`'s own
`[[geometry.identify]] axis = "file"`), which is where a player already expects something
to join up, not in the middle of a square.

**Build.** Replace the per-point "test against the centroid" with a **continuity walk**:
seed one known-good orientation, then choose each neighbouring sample's sign to agree with
its neighbour rather than with a global reference. This is stable everywhere (it never
divides by a near-zero quantity) and it places the one unavoidable flip exactly where the
walk stops propagating - which can be chosen to be the file axis's own wrap, by simply
*not* closing the loop.

- **The fine field.** Build a `normals_` grid parallel to `corners_` (same
  `cu = nx * kSubdiv + 1` by `cv = nz * kSubdiv + 1` indexing, `play_surface.cpp:140-148`).
  For each rank row `j` in `[0, cv)`:
  - Seed `i = 0` using today's test (`dot(normalAt(...), corner - axisAt[j]) `, flip if
    negative) - this is stable at `i = 0` precisely when `i = 0` is not itself a pinch
    (true for every `slideU` except the rare case the slide lands a pinch exactly on the
    lattice's own file-0 edge, where seeding from `i = 1` instead is a safe fallback).
  - For `i` from `1` to `cu - 1`: take the raw `normalAt(...)` at that corner, and keep its
    sign if `dot(candidate, normals_[i-1][j]) >= 0`, else negate it. **Do not wrap `i = 0`
    back to `i = cu - 1`** - that seam is where the one unavoidable flip is meant to land,
    and it is already the board's own glued file edge.
  - `nrm(u, vv)` becomes a bilinear lookup into `normals_` (the same four-corner
    interpolation `axisRef` already does for one axis, extended to both) instead of a
    fresh `normalAt` + centroid test - patch corners sample at `kCoverage`-inset points
    that do not land exactly on grid lines, so this is an interpolation of already-
    continuous values, not a re-derivation.
  - The `inverted` flip (`play_surface.cpp:174`, the INVERT feature) still applies once,
    uniformly, to every entry after the walk - it is a global swap keyed to the `evert`
    parameter, not a spatial choice, and does not interact with this.
- **The per-cell tangent.** Compute `ex` for every `(file, rank)` with the same walk, at
  cell resolution, *before* populating `seats_`: for each rank, walk `f` from `0` to
  `nx - 1`, seed `f = 0`'s `ex` as today (`normalize(flat)`, whatever sign that naturally
  gives), and for `f >= 1` negate the candidate `ex` (and recompute
  `ey = cross(normal, ex)` to match) when it disagrees in sign with the previous file's
  `ex`. Store the result in a temporary `nx`-by-`nz` array and have the existing per-cell
  loop (`play_surface.cpp:177-221`) read `ex`/`ey` from it instead of computing them
  inline - the loop's own nesting and the order `seats_`/`patches_` are populated in does
  not need to change, only where `ex` comes from.
- Both walks are **independent per rank** (a `v`-row's own `u`-walk does not depend on any
  other row), so this is no more expensive than today's per-sample work, done once instead
  of redundantly at every patch corner.

**Tests.** `tests/render/test_play_surface.cpp`:

- For `klein`, across every pair of **adjacent fine samples within the same rank** -
  i.e. `patch.normal[k]` and its immediate neighbour one `kSubdiv` step over, including
  across a cell boundary (e.g. file 2's last corner and file 3's first corner at the same
  rank) - assert `dot` of the two is positive and close to 1 (a few degrees of turn at
  most, never the ~170 degree flips measured above). This directly fails today and must
  pass after the fix; it is the test that would have caught this the way M17.13's
  uniformity test would have caught the clamp bug.
- The same adjacency check for `seat.quat`'s reconstructed `ex` across consecutive files
  at a fixed rank.
- **The one remaining discontinuity is exactly at the wrap**, not eliminated and not
  moved: `dot` of file `nx - 1`'s last fine sample and file `0`'s first fine sample, at
  the same rank, is allowed to be negative - assert this *can* happen (so a future change
  that "fixes" it by closing the loop and reintroducing a mid-board flip is itself
  caught), while every other adjacent pair in the same sweep is positive.
- Re-run with `slideU` at a few values spanning more than one cell, to confirm the two
  measured loci above are gone at every slide position, not just the ones probed by hand.
- `torus`/`cylinder`/`mobius` are unaffected (they have no pinch - `klein` is the only
  shape in the catalogue whose cross-section is a self-crossing curve) - the existing
  continuity/frame tests for those three must keep passing unchanged.

**Acceptance.** `chessbox_gui klein --geometry --shot k.ppm` at several `--slide` values
shows one continuous shape at every value - no captured frame shows a lit/shaded band that
reads as a second, differently-oriented object glued to the first. Dragging the middle
button through a full slide shows the squares and the pieces on them turning smoothly;
the Klein-specific comment in AGENTS.md about the crossing being "most of a right angle"
off should be revisited once this lands, since the continuity walk is the fix that comment
was asking for.

### Ordering

M17.13, M17.14 and M17.18 are regressions/defects found while playing with what shipped
and should be fixed before anything new is judged against them - M17.13 makes
`torus3d`/`hyper4` unusable to look at, M17.14 affects every shaped variant, and M17.18 is
specific to `klein` but makes it look broken rather than merely non-orientable. M17.8 and
M17.9 are one sitting each. M17.11 is a measurement and then a small fix. M17.7 and M17.10
are a button each plus one real piece of work (the outward side; a blended pipeline).
M17.15 is the dependency M17.16 and M17.17 both need - M17.16's camera has nothing honest
to follow before a move actually travels, and M17.17 only matters once the camera is close
enough to clip. Build them in that order: M17.15, then M17.16, then M17.17. [M17.12's
remaining limits](M17.12-shapes-above-two-dimensions.md) (level/aeon adjacency, no
slide/invert on the stacked shapes) are their own, larger step.

---

## Status: built for the glued 2-D case (2026-09-30)

Built on top of the M13 warps, with nothing new in the engine. The first cut drew the
surface as screen-space quads in the interface over the still-rendered flat board; that was
wrong - no camera control, and the flat board's labels showed through - so it was reworked
to draw the surface as real instanced geometry. That second cut placed every cell correctly
and still did not look like a board, for three separate reasons, and the third pass is what
is described here. See **ADR-0019**.

- **M17.1/M17.2** `render::PlaySurface` (`src/render/play_surface.cpp`) owns the placement.
  One sampling of `derivedSurfaceAt` gives each square the **patch of surface it is** - a
  grid of corners lying on the surface across the cell's own lattice footprint, with a
  little thickness - and each cell a *seat* for its piece, with a frame as a quaternion
  and a size read from its neighbours. `BoardRenderer::buildInstances` turns the patches
  into **one mesh**, rebuilt each frame with its colours in its vertices, and leaves the
  pieces instanced; both go through the session's own `OrbitCamera`, depth buffer and
  MSAA, so orbit and zoom work with no special path. The surfaces are rotated a quarter
  turn out of the overtures' Y-up world into the board's Z-up one - without that a donut
  stands on its rim like a wheel.

  Three earlier cuts of this drew the squares as *rectangles* and none of them read as a
  board. A rectangle has to be oriented, and a shortest-arc turn onto the normal left
  every square spun to its own angle; it has to be scaled, and the scale was being applied
  after the orientation in `board.vert`, which scales the world axes rather than the
  mesh's; and it has to be sized, which the surface's metric makes a moving target. Fixing
  all three left squares that are tangent at one point and therefore cut into each other
  where the surface turns fast - a Klein bottle's figure-eight turns most of a right angle
  per square - which is the "fish scales" the patch mesh exists to end. The scale-order fix
  stayed: the pieces still carry an orientation.
- **M17.3** `PlaySurface::pick` ray-tests a gapless grid of the same surface points the
  tiles were built from, with the session camera, so a click resolves to the cell under the
  cursor. The front end uses it instead of `Session::clickPixel` while the surface is on,
  and `Session::frameOn` puts the camera round the shape the moment the view is switched
  on - the flat layout's bounds are a different size in a different place, and framing on
  them left the board off in a corner.
- **M17.5** `Settings::geometryView`, a `SHAPE` button in the game rail, and the `G` key,
  offered only when `render::hasPlaySurface(v)` holds - a glued 2-D variant, never
  `standard`, `mirrorbox` or anything above two dimensions. `--geometry` captures it.
- **M17.6, new: the board is posed on its surface.** `SurfacePose` carries two poses, both
  pure functions of their numbers and both presentation only - they never enter
  `VariantId`.
  - `slideU`/`slideV` slide the board *along* the surface, in cells. It is the **middle
    mouse button** on the shape (the view centres the shape itself, so there is nothing
    left to pan) and `--slide`/`--slide-v` for a capture. Sliding one cell along the files
    is sampling at `u + 1/nx`, so a1 lands *exactly* where b1 was; keep going and the board
    comes all the way round, and a Moebius band comes home mirrored and needs a second lap.
    That is the gluing itself rather than a cyan line about it. Offered along the ranks
    only where the ranks are glued.
  - `evert` is the source of the INVERT control. As first built it geometrically everted
    the shape (the closed surfaces sweep their ring radius through zero to its negative; a
    cylinder rolls back over itself like a sock); **play found that a mirror, not an
    invert** - it moves every cell about the origin - so M17.7 below revises it to a swap
    of the outward side, with the squares fixed. `[` and `]` for a keyboard, `--evert` for
    a capture.

Two surfaces were also corrected while the board was being drawn on them, and both fixes
land in the overtures as well, since there is one catalogue:

- The Klein bottle's figure-eight cross-section is now walked at **constant speed**
  (`lemniscateAngle`). Equally spaced values of the lemniscate's angle land in pairs, so
  every other file came out twice the width of its neighbour. The reparametrisation is odd
  in the angle, which is exactly what keeps the rank seam's file reversal closing.
- The Moebius ribbon's stretch is a `SurfacePose` field. The library screen spends all of
  it on the twist being unmistakable, which leaves each cell nine times longer than it is
  wide; the play board keeps half, which is about as little as the shape will take before
  the ribbon is wider than the loop it goes round.

Coordinates for the flat/3-D board were moved off the cells to the board's near edges
(extrapolated one cell outward) in the machine face, and are skipped in the geometry view.
Changing the palette now re-applies the ImGui style, so console no longer leaves manifold's
dark type on its dark page.

**Deferred / limitations.**

- **The move animation is not warped.** A moving piece appears on the surface at its new
  cell rather than travelling the warped route; the route trail the overture draws is not
  yet emitted for a live move. The next increment: map the animation sample through the
  surface.
- **No surface coordinates or seam rails yet.** The surface's own labels and the portal
  rails along its glued edges are not drawn; on the shape themselves the seams are mostly
  self-evident, which is why this is the last piece rather than the first.
- **The eversion passes through degenerate embeddings** - the spindle torus is
  self-intersecting and the cylinder's fold has the surface doubled back against itself.
  They are drawn honestly rather than hidden: the degenerate moment is the one a player is
  watching for.
- **The D >= 3 case (M17.4) stays a no-op**: above two dimensions the ordinary extruded
  view already *is* the lattice, so there is no second shape to become and no toggle is
  offered.
- The surface mode is the game screen only; pause keeps the ordinary board.

Tests: `tests/render/test_play_surface.cpp` (the patches tiling the surface without
crossing and without being flat, the seat's frame and size, seam closure, the shape
standing up in the board's world, the slide putting a1 where b1 was and bringing the board
home after a lap, the eversion's identity at rest and its turn at full, continuity, and
picking following the drawing), `tests/render/test_offscreen_render.cpp` (a
validation-clean GPU render of `torus` on its surface) and
`ctest -R "gui-geometry|gui-evert|gui-slide"` (the capture flags).

---

## Status: M17.7 - M17.11 built (2026-09-30)

The next increment, done in the plan's order.

- **M17.8 - the drag axes are the right way round.** Middle-drag left/right slides along
  the ranks and up/down along the files; the `slidesAlongRanks` gate moved with it, so on
  a cylinder (ranks free) left/right does nothing and up/down carries the board.
- **M17.9 - a full lap, not a two-cell one.** The wrap lives in one place, in
  `PlaySurface`, in cells, as `fmod(s, 2 * nx)` / `fmod(s, 2 * nz)` - the board's own
  extent. `Settings::sanitize` no longer guesses `fmod(..., 16)`. The test now asserts the
  period reproduces the board, two cells does not, and a tenth of a cell moves no seat by
  half a cell.
- **M17.11 - the shape is centred.** Measured first: the board's centre was ~30 px below
  the window centre because `OrbitCamera::frame` lifts the look-at by its `headroom` for
  piece crowns, and `PlaySurface::bounds()` already pads for the pieces, so the target was
  double-counted. `Session::frameOn` takes a `headroom` and the geometry view passes 0.
- **M17.7 - INVERT, and pieces that stay outside.** A `INVERT` button beside `SHAPE`
  (shown while the shape is showing) sets `Settings::geometryInvert`, a *target* the front
  end eases `geometryEvert` to over ~half a second; `]`/`[` set the same target. **Revised
  after play:** the invert *swaps the outward side* rather than geometrically everting the
  shape - the surface functions' eversion mirrors the whole board about the origin and
  moves every cell, which is not what a board wants. `PlaySurface` keeps the geometry and
  negates the normal past the halfway point, so the squares stay put and the pieces move to
  the other face. The base outward normal is still chosen away from the shape's axis (the
  cross-section centroid, precomputed per rank), which is what makes even that negation
  land on a face rather than inside the material.
- **M17.10 - GHOST.** `Settings::geometryGhost` (0.35 to 1) with a `GHOST` button and a
  `--ghost` capture flag. The board mesh's alpha rides in `MeshVertex::color.a`; a second
  pipeline - same shaders and vertex input, `blendEnable` with src-alpha/one-minus-src-
  alpha, `depthWriteEnable = false` - draws it after the opaque pieces. The pieces stay
  opaque. Measured correction to the plan: a ghosted board's luminance *variance* is
  lower, not higher (blending averages the two sheets); the test asserts the picture
  changed, is validation-clean, and that `ghost = 1` is bit-identical.

Tests: the M17.7 pieces-outside-the-axis sweep over `evert` 0..1, the M17.11 framing test,
the M17.10 GPU render (`ghost` differs, `1` is a no-op), and `gui-invert` / `gui-ghost`
captures. `tests/render/test_play_surface.cpp`, `tests/render/test_offscreen_render.cpp`.

**Remaining: M17.12, the shapes above two dimensions** - `torus3d` as nested shells,
`hyper4` as a hypercube, `t6` as the quintic, with the adjacency invariant deciding whether
each embedding is a board or merely a picture. It is its own milestone-sized step and is
not started.

**M17.12, first pass (2026-09-30):** `torus3d` and `hyper4` now play on their own shapes.
The shape is *authored*, keyed by the variant's name (`playShapePosition`), because above
two dimensions it is not read off the gluing - `hyper4` is a plain 4-D box. `PlaySurface`
grows a stacked branch: one flat tile per cell from those positions, sized by neighbours,
with a per-tile picker. See [`M17.12-shapes-above-two-dimensions.md`](M17.12-shapes-above-two-dimensions.md)
for the limits (the level/aeon axes are not adjacent-in-the-drawing; slide/invert are not
offered yet) and for the `t6` verdict: **a faithful playable quintic is impossible** - the
6-torus projects ~10 lattice cells onto each drawn tile - so `t6` stays on the lattice, and
a playable quintic, if wanted, must be a *sliced* view.

---

## Status: M17.13 - M17.17 built (2026-09-30)

The second increment, in the plan's order.

- **M17.13 - the D >= 3 shapes are no longer fish scales.** `PlaySurface::buildStacked`
  took clamped file/rank neighbours, so a *periodic* `torus3d` axis returned every boundary
  cell its own position and the frames scattered; a *bounded* `hyper4` boundary took a
  halved one-sided step. The neighbour lookup now wraps a periodic axis and one-sides a
  bounded boundary, independently per axis. Tests: `stepU` uniform within a `torus3d` shell;
  a `hyper4` boundary cell no longer half the interior's.
- **M17.14 - the camera reframes on a variant switch.** `geometryView` is sticky across
  `Shell::startGame`, so a switch while it was on never reframed. The framing policy is now
  `render::frameGeometryCamera`, called unconditionally on a variant load.
- **M17.15 - a move on the shape animates.** `MoveAnimation::path()` exposes the route,
  `view::pointAlong` is shared with the camera, and `render::surfaceMoveSample` drives the
  travelling piece: a leap arcs outward, a glide walks the route's own seats, and a seam
  step is not a cut because on the surface the seam is one place.
- **M17.16 - the move camera follows on the shape.** `Session::cameraOver` runs the one
  blend against any placement set; the geometry view passes
  `surfacePlacements(PlaySurface::build(...))`. A click mid-shot is refused, as on the flat
  board.
- **M17.17 - ALIGN.** A facing search (`render::alignSlideU`, 16 slide candidates) picks the
  offset that turns the followed cell most toward the camera; `Settings::geometryAlign` (an
  `ALIGN` button, shown while following on a closed ring) eases a transient
  `geometryAlignOffset` into the pose. `torus`/`klein` only - an open tube or ribbon has no
  inner/outer side.

Tests: the M17.13 uniformity/boundary cases, the M17.14 reframe-on-load case, the M17.15
sampler (leap arc, glide visits, seam continuity) and a validation-clean in-flight render,
the M17.16 `cameraOver` no-op and follow cases, and the M17.17 facing search. All in
`tests/render/test_play_surface.cpp` and `tests/render/test_offscreen_render.cpp`.

**Remaining:** M17.12's larger limits (level/aeon adjacency on the stacked shapes, and any
sliced-`t6` view) and the M17.5 "no surface coordinates or seam rails yet" note.

---

## Status: M17.18 and the chase camera (2026-09-30)

- **M17.18 - Klein's normal field is continuous except at its seam.** `PlaySurface::build`
  replaced the per-point "flip against the cross-section centroid" with two **continuity
  walks**: a `normals` grid over the gapless corners (seed file 0 against the centroid, then
  each next sample agrees with the last, never closing the loop), and a per-cell `ex` walk.
  `nrm` is now a bilinear lookup into the continuous field. This moves the one unavoidable
  flip of a non-orientable shape off the lemniscate's numerically unstable pinch (where the
  centroid test divided by ~zero and returned noise two samples apart) and onto the seam the
  walk does not close. Tests: every adjacent fine normal sample and every adjacent seat frame
  is non-opposed, at several `slideU` values; the flanking file cells keep agreeing.
- **The chase camera (M17.16, revised).** Following a move on the shape is now **one
  continuous chase** rather than M11's lead/pull/cut: `render::surfaceChaseCamera` centres
  the piece, puts the camera behind it along the route's travel, and **rolls** the frame
  (`OrbitCamera::roll`, applied in `viewProj` and `pickRay`) so the piece's own up - its
  surface normal - is the view's up. So the piece stands vertically in the middle of the
  view, the way a third-person follow reads. `main::boardCamera` uses it whenever a shot is
  in flight on the surface and the settled framing otherwise. Test: every seat, projected
  through the chase, is centred with its normal pointing up the screen.

---

## Status: anti-clip by default, and two follow camera modes (2026-09-30)

Play then asked for the camera work to be the default, not opt-in, and for a choice of two
follow styles.

- **Anti-clip is on by default.** `Settings::geometryAlign` now defaults true and is the
  anti-clip: while following, `render::alignSlideU` searches slide offsets and scores each by
  (a) whether `PlaySurface::blocked` finds anything of the shape between the camera and the
  piece, then (b) the piece's `facing` - so a piece on a torus's inner ring is turned to the
  outer side rather than the camera clipping through the tube. The `NOCLIP` rail button
  toggles it, and the camera settings carry the same checkbox. `alignSlideU` now takes the
  route's travel direction and builds the camera its search is scored against.
- **Two follow modes.** `Settings::followUpright` (default true) makes the chase roll the
  frame so the piece's surface normal is the view's up - the piece stands vertically in the
  middle of the view. With it off the frame is not rolled, so the piece tilts and can flip
  with the shape. The `UPRIGHT`/`TILT` rail button and a settings checkbox choose between
  them. Both centre the piece and sit the camera behind it.

Tests: the anti-clip search now checks `blocked` returns false for the chosen offset and the
facing is positive; the chase test covers both the upright roll and the tilt mode's centred,
unrolled framing. `tests/render/test_play_surface.cpp`.

---

## Status: shape follow repaired, and a turntable mode (2026-10-01)

Play asked for the shape follow to be smooth, to keep the piece upright, and to stop the
camera clipping by **turning the board**; a strong GUI-less test was wanted for each.

- **Upright was inverted.** `surfaceChaseCamera` computed the roll as the turn from the
  unrolled up to the normal, but `OrbitCamera`'s `cameraBasis` applies `roll` in the
  opposite sense, so a piece's screen lean came out at twice its true tilt. The
  roll is now negated, and it is derived from the camera that was *actually built* after
  the pitch clamp and with `OrbitCamera::upHint()`, so it is exact near steep angles.
  `tests/render/test_play_surface.cpp` ("the chase camera's up really is the piece's up")
  checks the view's up axis against the piece's normal projected perpendicular to the
  view, at every seat and for both surface tangents; the old loose test did not catch the
  inversion.
- **Smoothness.** The chase eye is `-forward + normal*lift` (`forward` the route's travel,
  `lift` the elevation tangent), and the new `render::surfaceFollowCamera` samples the
  route through a window in `t` (a leap follows its chord) so the direction is continuous
  through a cell corner. `main::boardCamera` blends the follow onto the player's camera
  with `view::shotEnvelope * followStrength` (the same envelope the flat move camera uses),
  so a move eases in and out instead of snapping. Tested by "the follow camera is smooth
  along a move" - a fine sweep turns the view by a small angle each step.
- **Two selectable modes.** `Settings::shapeFollow`: `"chase"` (default and shipped) is
  the repaired third-person camera that follows behind the piece; `"turntable"` keeps the
  player's camera angle and turns the **board** - the shape-slide the middle-drag uses -
  so the followed piece comes round to the near side. The settings screen's `Follow shape`
  row toggles them, and `chessbox_gui ... --shape-follow chase|turntable` states it for a
  capture. `main::chaseEyeDistance` (0.7 * span) is used for both the chase camera and its
  anti-clip search. `render::alignSlideToFace` is the turntable anti-clip (a fixed eye
  direction rather than the chase's travel), built on the same offset search as
  `alignSlideU`. Both are tested: the turntable test brings every cell unoccluded on torus
  and Klein, and the chase's anti-clip keeps its existing facing/`blocked` test.
- **Anti-clip in captures.** `main::alignOffsetFor` is the deterministic form of the
  ALIGN search; the interactive loop eases towards it and a capture sets it outright, so
  a `--clip` of a followed move is anti-clipped like play.
- **The align offset reaches the renderer (the real bug).** `poseFrom` folded the transient
  `geometryAlignOffset` into the surface the camera and picker sampled, but `optionsFrom` -
  which the renderer builds the drawn board from - did not. So the camera aimed at the
  aligned surface while `BoardRenderer` drew the board unaligned: the align moved the
  camera but did not visibly turn the board. `optionsFrom` now adds the offset too, and a
  capture refreshes the renderer's options after setting it.
- **The chase searches occlusion at its own distance.** `alignSlideU`/`alignSlideToFace`
  take the eye distance the camera will actually use; before, the search tested at
  `0.6 * span` while a close follow sat inside that, so a seat it called clear could still
  be behind the tube. `main::chaseEyeDistance` (0.7 * span) is used for both the camera and
  its search. `tests/render/test_play_surface.cpp` pins it ("the chase align clears the
  shape at the camera's own distance").

---

## Status: two-axis anti-clip, non-orientable slides, and the follow elevation (2026-10-01)

Play asked for the piece to stay visible while the board turns, for a Klein bottle's
tiling not to tear when it is turned, and for the follow camera to sit above the piece by
an amount that can be set.

- **The anti-clip searches both surface axes.** `alignSlideU`/`alignSlideToFace` now
  return a `SlideOffset` (U *and* V) and sweep a 12x12 grid instead of a U-only sweep.
  `Settings::geometryAlignOffsetV` carries the V offset into `poseFrom`/`optionsFrom`, and
  the loop eases it like the U one. In the turntable test both remain unoccluded on the
  torus and the Klein bottle.
- **A non-orientable board is slid on the V axis only.** Measured on the Klein bottle's
  rank seam, the tile-corner gap while sliding U rose to about ten times an ordinary gap,
  while sliding V kept it near one; the U value was therefore removed from the middle-mouse
  turn (both drag axes drive V) and from the ALIGN search. Two tests pin this: "a V slide
  keeps the Klein rank seam whole" (V keeps the gap small) and "the Klein surface
  closes its gluing" (the immersion's rank join reverses the file coordinate).
- **The normal flip is hidden by two-sided shading.** The board push constant carries the
  eye position (`board.frag`), and a fragment turns its normal toward the viewer before
  lighting.
- **The follow camera's elevation is a setting.** The eye is behind the piece and above its
  tangent plane by `Settings::followElevationDeg` (default 30, slider 5-80); `surfaceChase
  Camera`/`surfaceFollowCamera`/`alignSlideU` take the tangent (`main::followLift`), so the
  camera and its anti-clip search use the same angle.
- **The Klein twist is a half-turn count.** `Settings::kleinTwist` (default 1) is how many
  half-turns the ring makes over a lap, carried straight into `SurfacePose::twist`. The
  figure-eight cross-section has one rotational symmetry (a half-turn), so only whole
  half-turns close the join; an arbitrary cell count would leave the ends open.
- **Shape width.** `Settings::geometryWidth` (slider, default 1) scales the ring or loop
  radius via `SurfacePose::openness`, leaving the cross-section - the material's
  thickness - unchanged. It opens a tight torus/Klein bottle's hole and lengthens the
  Moebius loop, which are otherwise hard to read at the shipped ratios.
