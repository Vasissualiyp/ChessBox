# M4 — Vulkan Renderer & Interaction

**Goal:** see and play any board the engine supports, including 3-D, 4-D and
higher, with seams and wrapping made legible. Deliberately narrow scope: one
material, instanced geometry, no PBR, no shadows, no post-processing.

**Exit condition:** a human can play standard chess, Raumschach, a 4-D variant
and a Klein-bottle variant with a mouse, at ≥ 60 fps, validation-layer clean.

## M4.1 Foundation

- SDL3 window/input; Vulkan 1.3 with dynamic rendering and `synchronization2`
  (no render-pass or framebuffer objects to maintain); VMA for memory; shaderc
  invoked by CMake to produce SPIR-V at build time (no runtime compilation).
- Device selection with explicit required-feature list and a readable failure
  message when unmet.
- Frames-in-flight = 2, per-frame descriptor and staging arenas, timeline
  semaphores. Bindless-ish single descriptor set for textures.
- Validation layers on in Debug; **a test run with validation errors is a failing
  test** (CI runs a headless swapchainless smoke frame). **[INVARIANT]**
- A `RENDERDOC=1` path documented for capture-based debugging.

## M4.2 Engine/renderer boundary **[INVARIANT]**

- Renderer consumes an immutable `PositionView` snapshot (double-buffered), never
  the live `Position`. No engine mutation from render code; no GPU waits on the
  engine thread.
- Test: a snapshot taken, then 1000 engine moves applied, then the snapshot read
  — the snapshot is unchanged (asserts genuine isolation, including field
  columns).
- TSan gate enters the picture here.

## M4.3 Instanced board rendering

- One instance per cell and per piece; instance buffer is a flat upload derived
  from the flat cell array — the engine's memory layout is already the GPU's
  preferred layout, which is why L3 is a flat array.
- Draw-call budget: O(piece types + cell state classes), independent of board
  size. Bench with 1e6 cells.
- Frustum/visibility culling on the projected layout, not on N-D cells.

## M4.4 N-D projection (the interesting part)

A declarative projection config, so new view styles are data:

```toml
[view]
screen_axes = ["x", "y", "z"]     # 2 or 3 axes rendered spatially
grid_axes   = ["w", "t", "l"]     # remaining axes laid out as a grid of boards
grid_spacing = [2.5, 2.5, 4.0]
```

- Axes not mapped to screen space become a grid of sub-boards (the 5D-chess
  presentation), recursively for more axes.
- Interactive axis remapping, slice scrubbing, and fold/unfold animation between
  configurations.
- The projection math is shared with the M2 TUI viewer and is unit-tested there
  in text form — the renderer inherits tested layout logic and only adds pixels.
- **Tests:** layout golden tests (cell → screen-space position) for 2-D..6-D
  configurations; property test that the layout is injective (no two cells
  overlap).

## M4.5 Seam and wrap visualisation

- Seam descriptors from M3.5 drawn as tinted edges with their transform labelled.
- Hovering a piece shows ghost previews of its moves *through* seams, drawn at the
  wrapped destination, plus an optional "unfolded" overlay showing the continued
  ray in the covering space. Without this, non-orientable play is unreadable.
- **Tests:** ghost-preview destinations equal the engine's generated moves
  (the renderer must never invent a move). **[INVARIANT]**

## M4.6 Interaction

- Pick via instance-id readback (not CPU ray-casting against N-D geometry).
- Click-to-select, legal-destination highlighting straight from movegen, drag or
  click-click move entry, promotion chooser, undo, move list, variant picker.
- Input is recorded as an action log, so UI flows are replay-testable headlessly.
- **Tests:** scripted action logs drive the whole app in a headless harness and
  assert the resulting game state — real UI tests, no screenshots needed.

## M4.7 Scope boundary for M4

Explicitly *not* in M4: themes/skins, sound, PBR materials, shadows, animation
polish, accessibility pass, settings UI beyond what is needed to play. These are a
later polish milestone so that M4 cannot expand without an ADR.

## Generalization test

A new view style is a `[view]` block. A new piece's visual is an asset row, not
renderer code.

## Acceptance facts

1. Standard chess, Raumschach, a 4-D variant and a Klein variant are all playable
   with mouse input.
2. ≥ 60 fps at 1e6 rendered cells on the reference GPU; recorded in bench.
3. Zero validation-layer errors/warnings in a full play session.
4. Snapshot isolation test passes; TSan clean.
5. Layout golden tests pass for 2-D..6-D; layout injectivity property holds.
6. Move ghosts equal engine-generated moves for every shipped topology.
7. Headless action-log tests cover select/move/promote/undo/variant-switch.

---

## Status: complete for its stated scope, with three deviations

Recorded 2026-09-28.

| Item | Status |
|---|---|
| M4.1 foundation | Done: Vulkan 1.3, dynamic rendering, synchronization2, validation-as-errors |
| M4.2 engine/renderer boundary | Done: `view::PositionView` snapshots, isolation asserted by test |
| M4.3 instanced rendering | Done: one draw call for the whole board, cells and pieces as instances |
| M4.4 N-D projection | Done in `view::layout`, shared with the ASCII board, golden-tested to 6 axes |
| M4.5 seam visualisation | **Not done** - see below |
| M4.6 interaction | Done: `app::Session`, scripted action logs, pixel→cell picking |
| M4.7 scope boundary | Held: one material, no shadows, no post-processing, no meshes |

### Deviation 1: no VMA

ADR-0006 named the Vulkan Memory Allocator. The renderer makes about five allocations -
two images, three buffers - so a sub-allocator would be a dependency carrying no weight.
Allocation goes through one helper and `VulkanContext::findMemoryType`, so introducing
VMA later is a change in one place. Revisit when assets arrive (M9) and the allocation
count stops being a handful.

### Deviation 2: CPU picking rather than instance-id readback

Recorded in ADR-0011. The short version: cells are axis-aligned boxes at known
positions, so a ray-box test is exact, needs no GPU, and makes the whole click path
testable headlessly - which an id-buffer readback would not be.

### Deviation 3: the camera lives in `view`, not `render`

It is presentation maths with no Vulkan in it. Putting it below the renderer means
framing and picking are testable, and a front end can use them, with no graphics device.

### The gap: seam visualisation

M4.5 asked for seams to be drawn and for wrap previews. Not implemented. Glued boards
render correctly and their legal moves highlight correctly - including moves that wrap,
because highlights come from the engine rather than from the renderer's own idea of
geometry, which `tests/unit/app` asserts. What is missing is the *explanation*: nothing
on screen says that the a-file and the h-file are the same edge. `Geometry::faceTransform`
already exposes the seam descriptors, so this is renderer work, not engine work.

Playing a Klein bottle without it is genuinely hard, so this should land before anyone is
asked to play one seriously.

### Also delivered here, because M4 needed it

`src/game` (L80) - history, undo, repetition, and result adjudication with the
stalemate policy the variant declares. It was in the M1 plan and was skipped; the CLI
had been keeping its own history instead. `app::Session` needed a real one.

### Acceptance facts

1. Standard chess, Raumschach-style `cube5`, 4-D `hyper4` and Klein-bottle boards all
   render and are playable through the session layer. **Holds** - though "playable" here
   means the interaction layer, which the windowed front end drives; only the window
   itself is untested, by necessity.
2. 60 fps at 1e6 cells. **Not measured.** No benchmark for the renderer exists yet;
   the draw-call count is O(1) in board size by construction, but that is an argument,
   not a measurement.
3. Zero validation errors in a full session. **Holds** for every shipped variant, as a test.
4. Snapshot isolation. **Holds.** TSan is still not meaningful - nothing is threaded yet.
5. Layout goldens for 2-D..6-D and injectivity. **Holds.**
6. Move ghosts equal engine moves. **Holds structurally**: highlights come from
   `Game::legalMoves`, and the renderer has no move logic to disagree with.
7. Headless action-log tests for select/move/promote/undo/variant-switch. **Holds.**

---

## M4.5: from tech demo to game

Recorded 2026-09-29, after the visual direction was agreed.

M4 produced a renderer that drew coloured boxes with a status line in the terminal.
That demonstrated the engine; it was not a game. This pass closed the gap.

**Visual direction.** One rule holds the look together: *warm is the game you know,
cold is the geometry you don't*. Amber is the candle - selection, headings, the primary
action - and cyan is spent **only** where the board stops being flat: seams, wrapped
moves, extra axes. A player learns in one game that cyan means "this edge is not where
it looks", and that only works because nothing else is allowed to use it. The palette is
`view::Theme`, a struct, so retheming - or letting a Workshop variant carry its own -
never means touching the renderer.

**Pieces are assembled, not loaded.** A variant may declare a piece nobody anticipated,
so models cannot be files. Seven archetypes are built at load from primitives, a variant
may name one (`shape = "horn"`) and set a height, and anything it leaves out is derived
from *how the piece moves*: oriented atoms give a pawn's dome, three-axis atoms give the
unicorn's horn, several sliding families give a crown. Height encodes value, so the
tallest piece in an unfamiliar army is the one that matters most. An unknown name falls
back to a tower - a variant is never unplayable for want of a model.

**The interface** is Dear ImGui restyled until it stops looking like Dear ImGui: square
panels, warm borders, chunky bevelled controls whose press lands on the accent, Cinzel
for anything naming a thing and JetBrains Mono for anything a machine produced. Left
rail: variant nameplate, board readout, library. Right rail: status, ledger, controls.
Promotion is a modal built from the variant's own `promotes_to` list, so a variant that
promotes to a unicorn offers a unicorn.

**Promotion now asks.** The session stops and reports a pending promotion rather than
picking a queen, because "queen" is a guess in a variant that might promote to anything.

**Two fixes worth recording**, both found by looking at a rendered frame:

- The camera framed the board against the whole window, so the rails covered a quarter
  of it. The board now has its own viewport - the gap the rails leave - and picking uses
  the same rectangle, or a click would select a different cell than the one under the
  cursor.
- Framing used the usual bounding-sphere approximation, which clipped the near rank: a
  board is a wide flat slab seen at a steep angle, and a sphere is a poor stand-in for
  what reaches the frustum edges. It now fits the actual corners exactly, in one pass,
  with a half-cell margin because the bounds describe cell *centres*.

**Seam visualisation**, the gap M4 recorded, is now half closed: a glued face draws a
cyan edge on the sides the geometry actually identified, so the two halves of a seam can
be paired by eye. Ghost continuations past a seam and the unfold animation are still to
come.

### How the interface is verified

`chessbox_gui --shot FILE` renders one frame - board, rails, ledger, promotion state -
into an image with **no display present**, using SDL's dummy video driver for the window
the interface needs and the same offscreen target the headless tests use. It exits
nonzero if the validation layers said anything, and `ctest -R gui-frame` runs it. That
makes the whole screen reviewable, and regressions in it catchable, on a machine with no
compositor.
