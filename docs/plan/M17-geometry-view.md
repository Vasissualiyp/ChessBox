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

---

## M17.7 - M17.12 The next increment: a shape you can handle

M17 shipped a board you can play on its own surface, and playing on it turned up six
things the first pass does not do. They are specified here, smallest first; each names what
is wrong, what is known about why, what to build and what pins it.

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

### Ordering

M17.8 and M17.9 are one sitting each and should go first - a control that is the wrong way
round and a slide that snaps make everything else harder to judge. M17.11 is a measurement
and then a small fix. M17.7 and M17.10 are a button each plus one real piece of work (the
outward side; a blended pipeline). M17.12 is its own milestone-sized step and should not be
started until the other five are done, because it generalises exactly the code they touch.

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
  - `evert` turns the surface through itself - the one move on a holed or non-orientable
    surface that no camera can show. The closed surfaces sweep their ring radius through
    zero to its negative, so the inside of the hole comes out by way of the spindle; a
    cylinder, which no rotation can invert, rolls back over itself like a sock. `[` and `]`
    for a keyboard, `--evert` for a capture.

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
  end eases `geometryEvert` to over ~half a second; `]`/`[` set the same target. The pose
  stays a pure function of its number. The outward normal is now chosen by the side away
  from the shape's axis (the cross-section centroid, precomputed once per rank), so the
  eversion's reversal of the parametrisation's handedness no longer leaves every piece
  inside the turned-out shape.
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
