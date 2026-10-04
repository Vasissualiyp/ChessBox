# Handover — next steps (Wave 1)

Written 2026-09-30 for an agent picking this up on another machine. Read
[`AGENTS.md`](../../AGENTS.md) first, then [`00-roadmap.md`](00-roadmap.md); this page is only
"what is done, what is next, and where".

## Where this is

M0–M6 are complete. Wave 1 (clippable visuals) is most of the way through; the work is
committed and the fast suite is green at the time of writing.

**Committed and green:**

- M11 the move camera — pure `view::moveCamera`, `routeRuns`, transported orientation,
  grid-axis cuts, `Session::camera()` integration, picking lock, `docs/camera.md`,
  ADR-0016/0017. Tests: purity, the frame-the-cell oracle, the turn, the cut, a safe-zone
  sweep over `klein`/`cube5`/`hyper4`/`t6`.
- M12.1 cinema mode and M12.2 clip export — `--cinema`, `--follow`, `--move-t`,
  `--clip DIR --frames --t0 --t1`.
- M13.1/2/3/5 generic overtures — `app::overtureSignature`, `derivedSurfaceAt`,
  `derivedOvertureScene`, the demo move, selection in `Ui`, `docs/overtures.md`.
- M13.4 the D ≥ 3 derived grid — `derivedGridOverture` (landed with this handover; compiles
  and the render/view tests pass, **not yet eyeballed** - see "Verifying a derived overture").
- M16.2 build pipeline — `tools/package.sh` (self-contained prefix + headless smoke test),
  `tools/steam_upload.sh` + `packaging/steam/`.
- M4.8 partial — 4× MSAA, `--bench-frame`, `Settings::frameCap`, fence instead of
  `vkQueueWaitIdle` in `present`.
- M4.9 partial — quintic memo, built-scene cache, adaptive subdivision.

**Not committed / minor:** nothing outstanding in the tree at handover.

## The next steps, in order

### 1. M4.8 remainder: two frames in flight, and rendering into the swapchain — BUILT

**Built 2026-09-30, ADR-0018 superseding ADR-0011.** The interactive path renders straight
into the acquired swapchain image (each image wraps it in a target that owns its MSAA,
depth and scratch), `Window` carries a two-slot frame ring, and the renderer records into
the caller's buffer so two frames overlap. The colour format is now a runtime property.
Files: `src/render/window.*`, `src/render/board_renderer.*`, `src/render/offscreen_target.*`,
`src/render/ui.*`, `src/gui/main.cpp`. A headless test drives the recording path into an
image with the swapchain's usages, both frame slots, validation-clean; all `render` and
`gui-*` tests pass.

**Needs a run on a real display before it is trusted.** `vkAcquireNextImageKHR` and
`vkQueuePresentKHR` need a surface, so the acquire/present sequence is the one part not
covered by a test. Run `./build/dev/src/gui/chessbox_gui standard` and check: the board
draws, the interface lays out, resizing works, pause (the defocus pass, now into the
swapchain image) looks right, and the validation layers stay silent.

### 2. M4.9 remainder: concrete `Pos` — MEASURED, DEFERRED

`render/overture_scene.cpp` still uses `using Pos = std::function<OvVec3(float, float)>`,
the one indirect call in the overture build. **Measured 2026-09-30 and deliberately not
done.** With the quintic memo and the built-scene cache in, `Pos` is off the steady-state
path: a pinned `t` reuses one build. Forcing a rebuild every frame via a `--clip`, the worst
case (`t6`, all 1600 quads) costs about **5 ms/frame more than a variant with no overture**,
at Debug `-O0` and including PPM IO. Making `Pos` concrete touches every builder in a
3100-line file to recover a fraction of a frame at `-O2`; the plan records the measurement
and defers it to the instanced-path rewrite (M4.9's "long-term fix").

### 3. M11.7: golden frames — DECIDED (no pixel images)

The plan asked for committed reference frames. **Decided against committing pixels**
(2026-09-30). A committed frame depends on the GPU driver **and** the compiler, and
`nix flake check` runs gcc and clang, so a pixel golden would be flaky or pinned to one
machine - the objection ADR-0011 already recorded. The driver-independent strength is
already in: the frame-the-cell oracle pins the pose at every shot boundary, the safe-zone
property sweeps random moves, and the topology/dimension tests pin the shot kinds. The
plan (`docs/plan/M11-move-camera.md`, M11.7) now records this; if a snapshot is wanted
later, pin quantised poses, not pixels.

### 4. M17: play on the shape (the geometry view) — BUILT for the glued 2-D case

New milestone, planned in [`M17-geometry-view.md`](M17-geometry-view.md). **Built
2026-09-30, reworked to the renderer after a first cut that drew the surface in the
interface over the flat board (no camera control, labels showed through).**

- `render::PlaySurface` (`src/render/play_surface.cpp`, new) owns every placement: the
  **patch of surface each square is**, the seat a piece stands on, and the ray a click is
  tested against - one sampling, so the pick cannot disagree with the picture.
  `BoardRenderer::buildInstances` turns the patches into one mesh with its colours in its
  vertices and leaves the pieces instanced; the session's own camera, depth and MSAA apply
  and orbit and zoom work. `standard --geometry` is a strict no-op.
- A square is a *patch*, not a rectangle: a flat tile is tangent at one point, so on a
  fast-curving surface the squares cut into each other at one end and gape at the other.
  Earlier cuts also got the frame (shortest-arc normals spun every square), the scale order
  (`board.vert` scaled the world axes, not the mesh's) and the size (the metric is not
  constant) wrong. See **ADR-0019**.
- **The board is posed on its surface:** `slideU`/`slideV` slide it *along* the surface -
  middle-drag on the shape, or `--slide`/`--slide-v` - so a1 rides to b1, round, and home
  (mirrored, on a Moebius band, after two laps). `evert` swaps the outward side - the
  squares fixed, the pieces moved to the other face, `INVERT`/`[`/`]`/`--evert`.
  Presentation only; neither enters `VariantId`.
- `Settings::geometryView`, `geometrySlideU`/`geometrySlideV` and `geometryEvert`, a
  `SHAPE` button and the `G` key, offered only where `render::hasPlaySurface` holds.
- Two surfaces were corrected in the shared catalogue: the Klein bottle's figure-eight is
  now walked at constant speed (8 files had been landing in pairs), and the Moebius
  ribbon's stretch is a pose field so the play board can keep less of it than the library
  screen does.
- Coordinate labels moved to the board's near edges in the machine face (flat/3-D only),
  and changing the palette now re-applies the ImGui style (console had been keeping
  manifold's dark type). Settings drop-downs became press-to-cycle buttons.

**Deferred** (see the M17 status section): the move animation is not warped, the surface
has no coordinate labels or seam rails yet, and the eversion passes through genuinely
degenerate embeddings (the spindle torus, the cylinder's own fold), drawn honestly.

**Next, and specified**: M17.7-M17.12 in the same plan.

**M17.7-M17.11 are built (2026-09-30).** The drag axes are swapped (left/right along the
ranks, up/down along the files, M17.8); the slide wraps after two laps - `fmod(s, 2*nx)` in
cells, in `PlaySurface` alone (M17.9); the shape is framed on its own centre with
`headroom = 0` (M17.11); an `INVERT` button and `]`/`[` set a target the front end eases,
and the outward normal is chosen away from the shape's axis so pieces stay outside after the
eversion (M17.7); a `GHOST` button and `--ghost` blend the board through a second pipeline
with depth writes off (M17.10). Tests and `gui-invert`/`gui-ghost` captures are in.

**Remaining: M17.12, the shapes above two dimensions.** First pass done (2026-09-30):
`torus3d` (nested shells) and `hyper4` (tesseract) play on their own shapes via an authored,
name-keyed `playShapePosition` and a stacked branch of `PlaySurface`. Limits and the `t6`
verdict are in [`M17.12-shapes-above-two-dimensions.md`](M17.12-shapes-above-two-dimensions.md):
the level/aeon axes are not adjacent-in-the-drawing, slide/invert are not offered for these
shapes yet, and **a faithful playable quintic is impossible** (the 6-torus projects ~10
lattice cells per drawn tile), so `t6` stays on the lattice and any playable quintic must be
a sliced view. Finish the 3-D shapes (slide/invert, hidden-sheet handling, adjacency tests)
before the sliced quintic.

### 5. Found 2026-10-03, playing it: the chase camera goes into the board, and jitters

**Fix this before anything else below.** Reported during a manual playtest pass: a followed
move's camera on a shape (`shapeFollow = "chase"`) sometimes clips into the board and
sometimes jitters. Reproduced and root-caused: `alignSlideU`'s anti-clip search
(`src/render/play_surface.cpp`) scores a *different* eye direction than the one
`surfaceChaseCamera` actually draws (the search uses the raw surface normal; the real
camera first removes its component along the travel direction), so a rotation the search
calls "clear" can still be looking straight into the shape - and the existing property
tests cannot catch it, because they reimplement the same (wrong) formula instead of calling
the real one. A second, compounding bug: when nothing in the search grid is actually clear
at the nominal eye distance, it silently falls back to the best-*facing* (still occluded)
candidate rather than ever pulling the camera closer. Full root-cause writeup, the fix (a
shared `chaseEyeDirection`/`clearEyeDistance` primitive used by both the search and the
camera, so they cannot diverge again), and the regression tests that would have caught it:
**[`M17-geometry-view.md`](M17-geometry-view.md), §M17.20.**

### 6. Then the rest of Wave 1, fully spec'd and ready to build, in this order

Spec'd 2026-10-03 for a handoff to opencode (see each file for the detailed build/tests/
acceptance - this is only the index and the order):

1. **M17.20** above - fix first; M12.6 below records through this camera, so a clip of it
   is only worth having once this is fixed.
2. **M17.21** - seam rails and coordinate labels on the shape (the M17.5 remainder):
   [`M17-geometry-view.md`](M17-geometry-view.md), §M17.21.
3. **M17.22** - slide/invert/ghost-picking for the D>=3 stacked shapes (`torus3d`,
   `hyper4`), pinned adjacency tests, and a new twisted-3-torus variant (`flip` on one
   `torus3d` axis, same trick `klein.toml` already uses in 2-D) with the shape view
   extended to show the twist:
   [`M17.12-shapes-above-two-dimensions.md`](M17.12-shapes-above-two-dimensions.md).
4. **M12.6** - the marketing notation runner (`--play FILE [--clip DIR]`), now including
   making the M17.19 shape choreography a pure, deterministic function of elapsed time (it
   currently only runs in interactive play - captures use the plain steady-state follow on
   purpose, and that default must not change) and `--width`/`--height` capture flags
   (default bumped to **1920x1080**, Steam's trailer minimum; was a hardcoded 1440x900):
   [`M12-broadcast.md`](M12-broadcast.md), "M12.6 implementation spec (2026-10-03)".

Trailer-priority variants, from a planning conversation the same day: `standard`,
`torus`/`klein`/`mobius`, `cube5`/`hyper4`, plus `5d` (time travel, flat board, no shape-
view work needed), `atomic_torus` (explosion + wrap) and `torus3d` (the twisted-variant's
untwisted sibling) - see §M17.22 for why `torus3d` specifically rounds out the D>=3 set,
and `t6` stays lattice-only (no shape view planned; see the Verdict in
`M17.12-shapes-above-two-dimensions.md`).

### 7. Then Wave 2

M14 onboarding, M16.3 the demo (M16.2 is done), M7 the editors. See the release sequence in
the roadmap.

## Verifying a derived overture (no shipped variant)

The derived path only triggers for a variant whose name is in no C++ table. To see one:

1. Write `variants/_demo.toml` (any name, e.g. a 2-D torus or a 4×4×4 box). A worked example
   is in `tests/render/test_overture_scene.cpp` ("a data-only variant gets a derived
   overture") and the two structures are in `variants/standard.toml`.
2. `./build/dev/src/gui/chessbox_gui _demo --shot out.ppm --screen newgame --t 1.0`. The
   capture forces the library picker to the named variant, so the derived scene is what is
   drawn.
3. **Delete the file before committing** - `tests/golden/test_shipped_variants.cpp` scans
   `variants/`, so a stray temp variant becomes a shipped one and breaks the goldens.

## Commands (from `AGENTS.md`)

- `tools/test.sh --build <tag...>` — only the areas a change can touch (tags: `view`,
  `render`, `app`, `io`, `game`, `rules`, `variant`, `pieces`, `golden`, `arch`).
- `tools/test.sh quick` — every fast ctest label (`unit render golden arch` + gui).
- `ctest --preset dev` — the whole fast suite (adds the property sweep and perft).
- `tools/package.sh` — self-contained package + clean-directory smoke test.
- Captures: `--shot`, `--clip`, `--cinema`, `--follow off|piece|route`, `--move-t 0..1`,
  `--bench-frame N`.

## Gotchas that will bite on a fresh machine

- **A build directory remembers its shell.** Anything touching the renderer, the UI or the
  GUI must be configured and built under `nix develop .#gfx`; the plain shell has no
  SDL/Vulkan headers.
- The `--shot`/`--clip` path is settled by **two thrown-away frames** before the real ones
  (a screen-change resets the pane transition after the tick; the second advances it). One
  frame and the library draws at alpha zero.
- Temp variant files must be deleted before committing (see above).
