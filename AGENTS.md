# AGENTS.md — ChessBox

Read this first. It is the map. Design rationale is in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md); the plan is in
[`docs/plan/00-roadmap.md`](docs/plan/00-roadmap.md); decisions are in
[`docs/adr/`](docs/adr/README.md).

**Status: M0-M6 complete.** A playable, perft-exact engine with N-dimensional boards,
non-trivial boundary topology, a Vulkan interface, a rule VM, and 5D-chess-style time
travel. Next: M7 (the GUI editors), M8 (multiplayer), M9 (Workshop), M10 (AI).
See [`docs/plan/00-roadmap.md`](docs/plan/00-roadmap.md); each milestone plan ends with
a status section recording what was built, what was deferred, and why.

## What this project is

A FOSS sandbox for finite-board game variants of arbitrary shape: any number of
dimensions, arbitrary boundary topology (torus, Klein bottle, …), piece movement
expressed in one vector-move algebra, custom data fields, and 5D-chess-style time
travel. Engine core is dependency-free C++23 driven by declarative variant data.
Performance and extensibility are the two hard constraints.

## Commands

```bash
nix develop                      # core dev shell (no graphics deps)
nix develop .#gfx                # adds SDL3 + Vulkan (M4+)
cmake --preset dev && cmake --build build/dev
ctest --preset dev               # fast suite; the slow label is excluded by the preset
ctest --preset dev -L unit       # the tightest loop; labels: unit property golden perft arch
ctest --preset release-slow      # deep perft + wide property sweep - RELEASE, it is ~10 min
                                 # (the same in Debug is over an hour: depth-5 perft at -O0)
tools/precommit.sh               # format + tidy + fast tests — run before every commit
nix flake check                  # THE gate: both compilers, sanitizers, coverage, goldens, bench

./build/dev/src/cli/chessbox                          # interactive
./build/dev/src/cli/chessbox "load torus" board moves  # batch; nonzero exit on error
./build/release/bench/chessbox_bench                   # release only

# The game. Needs the gfx shell; run from the repo root so variants/ resolves.
./build/dev/src/gui/chessbox_gui                       # standard chess
./build/dev/src/gui/chessbox_gui klein                 # any variant in variants/
./build/dev/src/gui/chessbox_gui cube5 --shot out.ppm --script "click c1"
./build/dev/src/gui/chessbox_gui --shot out.ppm --screen settings   # any screen
./build/dev/src/gui/chessbox_gui torus --shot o.ppm --screen newgame --t 0.6  # an overture
./build/dev/src/gui/chessbox_gui standard --shot o.ppm --screen body   # the designer tabs
./build/dev/src/gui/chessbox_gui standard --shot o.ppm --screen designer --dims 4
```

**A build directory remembers which shell configured it.** One configured under
`.#gfx` will not build under the plain shell - the Vulkan and SDL headers are gone.
Use `.#gfx` for anything touching the renderer, the interface or the GUI.

`--shot` renders one frame - board, rails, ledger, everything - to an image with **no
display required**, and exits nonzero if the Vulkan validation layers said anything. It
is how the interface gets reviewed, and `ctest -R gui-frame` runs it.

The CLI is the fastest way to inspect anything: `info` prints a variant's axes,
geometry and canonicalised atoms; `board` renders N-D boards as labelled slices;
`divide <n>` splits perft by first move.

## The rules (non-negotiable)

1. **Test first.** No production line before a failing test names the behaviour.
2. **Green before next step.** A red suite blocks all forward motion — not just
   the next milestone, the next step.
3. **Never "update" a golden to make a build pass.** Investigate.
4. **The naive oracle in `tests/oracle/` is sacred.** Every optimisation is proven
   equal to it by differential test. See ADR-0009.
5. **No floats in `chessbox_base` or `chessbox_core`.** Determinism. Enforced by a test.
6. **No allocation in movegen.** Enforced by the allocation tripwire.
7. **Architectural decisions get an ADR.** Immutable once merged; supersede, never edit.
8. **Update this file in the same commit** as anything that changes navigation or commands.

## Layer map — dependencies point strictly down (ARCH §1)

Each layer is a CMake target linking only to lower layers, so a violation is a
**link error**, not a review comment.

| Layer | Dir | Holds |
|---|---|---|
| L110 | `src/cli` | the scriptable command-line front end |
| L110 | `src/gui` | the playable window's main loop (exe only) |
| L100 | `src/render` | Vulkan, offscreen target, instanced renderer, piece meshes, ImGui panels, variant overtures |
| L105 | `src/net` | the multiplayer front end: framed protocol, in-process and TCP transports, fault injector, authoritative server, thin client |
| L95 | `src/app` | screens, settings, interaction: actions in, snapshot out |
| L90 | `src/io` | variant TOML loader, notation, FEN-N, ASCII board, replay |
| L80 | `src/game` | history, undo, adjudication, repetition, clocks |
| L70 | `src/temporal` | turn/timeline axes, present, branching (M6) |
| L60 | `src/view` | N-D projection, camera, picking, snapshots, the theme |
| L50 | `src/movegen` | expansion, ray walk, staged gen, legality, perft |
| L40 | `src/position` | cells, occupancy, field columns, hash, make/unmake |
| L30/35 | `src/pieces`, `src/variant` | vector-move algebra, resolved `VariantSpec` |
| L25 | `src/assets` | authored content: a piece's body and flat icon, as integer vector data |
| L20 | `src/geometry` | identifications, transition group, transport |
| L10 | `src/space` | `DimSpec`, `Coord`, `Direction`, strides, `CellId` |
| L0/5 | `src/base`, `src/diag` | containers, arenas, `Result`, bitsets, RNG, Zobrist, tracing |

Levels must be unique; an arch test enforces it. `src/view` and above may use floating
point, the layers below may not - that boundary is the whole point of where `view` sits.

## Three representations of "a square" — get this right (ARCH §2)

- `Coord` (18 B POD, `(x,y,z,…)`) — **only** L9 IO and L10 UI and tests.
- `CellId` (`uint32`, flat lattice index) — **everything hot**. Movegen never
  decodes a coordinate.
- `DirId` (`uint16`, index into the variant's global direction table).

## Where to add what

| Task | Go to | Skill |
|---|---|---|
| New piece | `variants/*.toml` (data only) | `cb-new-piece` |
| New variant | `variants/`, goldens, `docs/variants/` | `cb-new-variant` |
| New topology | `variants/*.toml` geometry block | `cb-new-geometry` |
| New rule mechanic | `src/rules/` opcode + tests + docs table | `cb-new-effect` |
| A piece's look | `variants/*.toml` `shape` / `height` (data only) | `cb-new-piece` |
| UI colours | `src/view/theme.hpp` - two themes, one struct, no renderer changes | |
| A flat piece icon | `src/render/piece_icon.cpp` - polygon tables, two styles | |
| An authored piece body or icon | `src/assets/piece_model.cpp`; the designers are `src/render/ui_designer.cpp` | |
| A menu's decorative object | `src/render/deco.cpp` + `decoForScreen` | |
| How a menu object answers a drag | `Ui::updateObjectDrag`; the turn is an offset on the object's own animation | |
| A variant's library animation | `src/render/overture_scene.cpp`; which one, and its cycle, in `src/app/overture.cpp` | |
| New module in a layer | that layer's dir + CMake edge + test + this table | `cb-new-module` |
| A decision | `docs/adr/` | `cb-adr` |
| Perft mismatch | bisect with `divide` against the oracle | `cb-perft-golden` |
| Performance work | `bench/baselines/` first, then the hot path | `cb-bench-baseline` |

Variants ship, all as data: `standard`, `cylinder`, `torus`, `mobius`, `klein`,
`mirrorbox`, `cube5` (3-D), `hyper4` (4-D), `torus3d`, `t6` (6-D torus), `atomic`,
`atomic_torus`, `mustcapture`, `charged`, and `5d` (turn and timeline axes). See
[`docs/variants/README.md`](docs/variants/README.md).

Skills live in `.claude/skills/` and are catalogued in
[`docs/plan/skills.md`](docs/plan/skills.md). Prefer them: the procedures are
long, multi-directory, and identical every time.

## Conventions

- C++23. `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`.
- Types `PascalCase`, functions `camelCase`, members `trailing_`, constants `kName`.
- SPDX header on every file: `// SPDX-License-Identifier: GPL-3.0-or-later`.
- Errors: `Result<T, ErrorCode>` in the engine; exceptions only at the IO boundary.
- Tests mirror the source tree: `src/movegen/ray.cpp` → `tests/unit/movegen/test_ray.cpp`.
- Commits: `<area>: <imperative summary>`; a golden change must explain itself.

## Gotchas

These are the ones that have actually cost time here, not hypotheticals.

- **A VariantSpec must outlive, and must not move under, every `Position` and
  `MoveGen` that refers to it.** They hold a pointer to it. Own it somewhere stable;
  the CLI's `Session` is non-movable and heap-held for exactly this reason.
- **Direction transport, not just position.** A piece crossing a seam has its
  *direction* mapped too, or non-orientable boards are silently wrong. The transform's
  action on directions is derived, never authored, so it cannot be declared wrongly.
- **Ray semantics on glued boards are subtle — read ADR-0010 before touching them.**
  Rays end on orbit closure (`cell` *and* `direction` back to the start), duplicates
  are removed from the move list rather than by cutting the ray short, and
  `isAttacked` uses a forward scan on glued boards because the fast backward walk is
  unsound there. Each of those was a real bug first.
- **An oriented atom's direction span is not closed under transport.** A Klein seam
  turns "forward" into "backward", so backward attack search uses the *unoriented*
  expansion, per colour, and checks the arrival direction.
- Direction counts explode with dimension (`[1,2,3]` at D=8 → 2688). There is a
  load-time budget; respect it.
- An atom of order `r > D` expands to *nothing* — deliberate, tested, not an error.
- "Forward", "the last rank", castling and bishop colour-binding are meaningless on
  some topologies. Per-variant policy; never hardcode. A glued board also needs a
  different *opening array* — the usual one starts in check on a torus.
- **Do not specialize on dimension count.** The ray walk iterates a direction's
  support (1–3 axes), not the dimension count; `docs/plan/M2-nd-generalization.md`
  records the measurement reasoning.
- `kMaxDims = 8` lives in `src/space/dims.hpp` and nowhere else.
- **A piece model is integer permille and has no floats in it.** `src/assets` is content:
  it is saved, shared and eventually signed, so two machines must agree on it byte for
  byte. Trigonometry belongs to the assembly step above it. Geometry is cosmetic and does
  **not** enter `VariantId` - changing how a bishop looks must not make a saved game
  unreplayable.
- A flat piece is a figure in its *own* colour on a token that is the **same** for both
  sides. Drawing white as dark-on-light and black as light-on-dark makes the disc the
  thing the eye sorts by, and the disc is the furniture. `Theme::pieceToken` is the one
  colour that has to clear both piece colours, which pins it into a narrow band.
- An editor page that draws its own frame must be dispatched *before* `buildEditor` opens
  the shared one - a page that begins a second pane inside the first leaves ImGui holding
  both, and says so in a red box.
- Placing a widget with `SetCursorScreenPos` moves the layout cursor. Put it back, or
  everything drawn after lands inside whatever you were drawing on.
- **`widgets::fillPolygon`, never `PathFillConcave`, for a piece outline.** ImGui's own
  concave fill closes a valley over - a notch an author cuts comes back filled - so the
  fill here ear-clips instead. It *copies* its points first, and that is not tidiness:
  callers pass `dl->_Path`, and every triangle drawn rewrites `_Path` underneath the loop
  reading it. The symptom is vertices in the 1e33 range, not anything that looks like a
  drawing bug. `triangulate` fills; it does not validate - `assets::validate` is what
  decides whether an outline is a shape at all.
- The piece designer is one pane with three tabs, and the two model tabs draw *inside*
  it. An editor page that opens its own pane must be dispatched before `buildEditor`
  opens the shared one.
- The designer's move preview builds a real `DimSpec` and runs the real `expandAtom`, so
  "what does this piece do on a 4-D board" is answered by the engine rather than by a
  drawing. Extent shrinks as dimension grows - 9, 7, 5 - because 9^4 is unreadable.
- **An atom's magnitudes are units of *movement*, not cells** - see ADR-0013. An axis
  declares a `pitch` for how many cells one unit covers; the turn axis uses 2, because
  boards along it alternate whose move it is and one turn of travel is two boards. The
  scaling is applied once, where the direction table is built, so nothing below the
  variant layer ever sees anything but lattice cells. The oracle applies the same rule
  from its own code, deliberately.
- **Pieces have no model files.** A shape is assembled from primitives against an
  archetype (`src/render/piece_mesh.cpp`); a variant may name one, and anything it
  leaves out is derived from how the piece moves. An unknown name falls back rather
  than failing - a variant is never unplayable for want of a model.
- The palette is `view::Theme`, one struct, and there are two of them: `manifold()` is
  the shipped look - a light shell with a board several steps darker than the page - and
  `console()` is the older candlelit one. Retheming must not touch the renderer.
  `tests/unit/view/test_theme.cpp` pins the load-bearing part: both piece colours must
  read on both squares. That test found the old board pair sitting at 1.2:1.
- Saturated colour is reserved for geometry that is not flat: seams, wrapped moves,
  extra axes. The shell itself is cool and neutral, so spending a seam hue on a button
  breaks the one rule a player learns by playing.
- **A seam is coloured by the portal it belongs to, not by being a seam.** Both ends of
  one identification share a colour off a hue ramp, so a cylinder reads as one gradient
  repeated and a Moebius band as one folded - and brightness, not hue, distinguishes two
  portals that a twist puts on the same face. `src/view/seams.cpp`.
- **`Move::captureCell` is not `Move::to`.** They differ for exactly one move - en
  passant, where the square landed on is empty - and reading `to` to decide whether a
  capture atom applies made the move look illegal. That left the animation with a route
  of no steps, a one-point polyline, and a crash. `src/view/move_anim.cpp`.
- The three interface typefaces come from the flake as `CB_FONT_DISPLAY`,
  `CB_FONT_BODY` and `CB_FONT_MONO`, and are copied beside the binary by
  `src/render/CMakeLists.txt`. Chakra Petch is not in nixpkgs and is fetched by hash at
  a pinned commit. A missing font is never fatal - the interface falls back and looks
  plainer.
- **Every hardcoded pixel size in the interface goes through `Ui::px()`.** A missed one
  scales the text and leaves the panel behind. `widgets::menuEntry` takes its scale as a
  required argument for exactly this reason - a default let four menu rows draw at the
  wrong size and the menu looked ragged.
- Draw overlays from `ImGui::GetItemRectMin()`, never from the cursor position captured
  before the item: they are not always the same.
- The seam rim is a constant *world* width, derived in the shader from the instance's
  scale. A fraction-of-the-box band turns the board's plinth into a solid orange slab.
- Debug is pinned to `-O0` because nix injects `-O2`; a Catch2 `[.]`-hidden test still
  runs if a filter matches any of its other tags.
- **An overture is a pure function of `t`.** `overtureScene(which, t, intro, theme)` reads
  no clock and no previous frame, which is what makes reverse playback free - the player
  counts `t` down - and a screenshot reproducible. Anything that needs to remember
  something between frames belongs in `app::OverturePlayer`, not in a scene.
- **`--shot` hands the interface a fixed timestep on purpose**, and the interface hands
  that same timestep to the overture as its dt. `setProgress` therefore *pins* the player
  so the capture stays where it was put; pinning without holding put `--screen newgame`
  almost half a cycle past the `--t` it was given.
- A Klein bottle cannot be closed with a circular cross-section. `klein` glues its ranks
  with the file reversed, and a half-turn brings a circle back *shifted*, not reflected -
  so `kleinSurf` pinches the cross-section into a figure-eight, for which a half-turn
  *is* the reflection. `tests/render/test_overture_scene.cpp` states each variant's own
  gluing as arithmetic, which is the test that caught it.
- `ImDrawList::AddCircle` with `num_segments = 0` divides by a tessellation table that is
  zero-filled until `NewFrame` has run - so it is an integer divide by zero in a headless
  draw list. Always state the segment count.
