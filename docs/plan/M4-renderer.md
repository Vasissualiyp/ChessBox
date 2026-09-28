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
