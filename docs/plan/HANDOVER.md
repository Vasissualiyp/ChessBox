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

### 1. M4.8 remainder: two frames in flight, and rendering into the swapchain

This is the one Wave-1 item left and the riskiest. Details and the full rationale are in
`docs/plan/M4-renderer.md` under "M4.8 as built … Deferred". The shape of it:

- The interactive path renders into an `OffscreenTarget` and blits to the swapchain; that is
  ADR-0011, so removing the blit **supersedes an accepted ADR** and needs a new one that
  supersedes it.
- The gain is input-to-photon latency only (the plan says so: under FIFO vsync it does not
  raise the frame rate), and the interactive `present` path has **no headless test** -
  `--shot` and every `gui-*` test exercise the offscreen render, not `Window::present`. Plan
  on running the game on a real display before trusting it.
- Sketch: give `BoardRenderer` a small frame ring (2 command buffers, fences and semaphores,
  one per in-flight frame), double the `OffscreenTarget` in `gui/main.cpp`, have the render
  submission signal a semaphore and `Window::present` wait it; or render straight into the
  acquired swapchain image with a per-image depth image and a resolve target for MSAA.
  Files: `src/render/vulkan_context.*`, `src/render/board_renderer.*`, `src/render/window.*`,
  `src/gui/main.cpp`.

### 2. M4.9 remainder: concrete `Pos`

`render/overture_scene.cpp` still uses `using Pos = std::function<OvVec3(float, float)>`, the
one indirect call in the overture build. The plan asks for a small variant/template that
`addGrid` can inline. **Measure first** with `--bench-frame`: a `quinticPoint` call is
~112 ns and the memo already removed most of them, so this may not be worth the churn; if it
is, the refactor is mechanical but touches every builder in the file. Adaptive subdivision
is already done.

### 3. M11.7: golden frames (decide, do not blindly commit images)

The plan asks for committed reference frames. A committed frame depends on the GPU driver
**and** the compiler, and `nix flake check` runs gcc and clang, so a pixel golden would be
flaky. The driver-independent half is in (the safe-zone property test); if a golden is still
wanted, pin integer/quantised **poses**, not pixels, and say so in the plan.

### 4. M17: play on the shape (the geometry view)

New milestone, planned in [`M17-geometry-view.md`](M17-geometry-view.md). A button turns the
play board into its own topology (the torus is a donut, the Klein bottle a bottle) and the
game stays playable on it. It reuses the M13 warps, so it is small on top of work already
done, and it is placed **before M16.1** in the release sequence because it is the strongest
possible trailer. The hard part is picking on a curved, possibly non-orientable surface;
the choice to make there is documented in the plan (ray-cast the drawn quads, which cannot
disagree with the drawing).

### 5. Then Wave 2

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
