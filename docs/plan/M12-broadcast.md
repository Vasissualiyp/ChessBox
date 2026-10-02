# M12 — Spectator, Replay and the Clip

**Goal.** Turn the move camera (M11) into something a person can *watch*. M11 makes a
move readable in-game; M12 makes it presentable — a chrome-free cinema view, a
deterministic replay, and a clip that can be exported and shared. This is the layer that
turns "a chess-variant sandbox" into "a game whose moves are worth watching", which is
the difference the store page and a stream both live or die on.

**Exit condition.** A player can watch a game (their own recorded one, or — with M8 — a
live one) through the move camera with no interface furniture, replay the last move, and
export a short deterministic clip of a chosen move to a file, on any geometry, with the
same frame every time it is exported.

**A note on placement.** M12 splits across two release waves (see the roadmap). The
**export half** — cinema mode and deterministic clip export (M12.1, M12.2) — is Wave 1,
immediately after M11, because the clip is the thing that gets promoted and it needs
nothing but the camera. The **sharing half** — authored presets (M12.4) and live
spectating (M12.5) — is Wave 4, because it needs M9 (packages) and M8 (the net). M11 does
not depend on M12, so the camera ships value on its own; M12.4/M12.5 can slip without
holding anything back.

---

## M12.1 Cinema mode

A presentation state distinct from the game HUD, driven by the same `boardRect`/overlay
seam the renderer already takes (`src/render/board_renderer.hpp:122`).

- Hide rails, ledger, highlights, coordinate labels and the selection ring while a shot
  plays, so the frame is the board and the move. The UI already computes the board rect
  (`src/render/ui.cpp:933`); cinema mode widens it and suppresses the overlay.
- A **letterbox** option (bars top and bottom) and a neutral backdrop, so a clip reads as
  footage rather than as a screenshot.
- A **spectator camera profile**: the move camera plus a slow ambient orbit between
  moves, so a still board is not dead. The ambient orbit is a pure function of wall
  time and is *not* part of the deterministic shot — it must not enter a captured frame.
- Cinema mode is a `Settings` value (`cameraMode = cinema`) alongside M11.5's, and a
  command-line flag (`--cinema`) so `--shot` can capture the presentation itself.

## M12.2 Deterministic clip export

The `--shot` path already renders a frame headlessly with no display and fails on any
validation-layer message (`src/gui/main.cpp`). A clip is that, repeated at a fixed step:

- `--clip out.<ext> --move <n> --t0 --t1 --step` renders the move camera at fixed `t`
  increments, one frame each, and encodes.
- **Encoding stays dependency-light.** Reuse the existing image writer
  (`src/render/image_io.hpp`) for a sequence, then mux with an optional external tool if
  present; the engine core gains no codec dependency. A directory of numbered PPM/PNG
  frames is the guaranteed output; a single video/GIF file is best-effort.
- **Determinism is the contract:** the same `(variant, position, move, t-range, step)`
  yields byte-identical frames, because `moveCamera` is pure (M11.2). The clip is a
  golden test.
- **Export cost is per-frame CPU, not GPU.** Every exported frame draws the board and
  the interface once, so wall time is dominated by the same instance building and CPU
  menu geometry that play pays (M4.8/M4.9); the GPU is idle throughout. The exporter
  should report frames/second so a long clip is a known duration rather than a surprise,
  and it benefits directly from M4.8's pipelining and M4.9/M13's cheaper or instanced
  menu objects.

## M12.3 Replay and highlight

- A **replay** command re-runs the last move's shot sequence from `t=0`, then the
  previous, then the previous — a "watch the last few moves" scrubber. Deterministic and
  `--shot`-able.
- A **move history** scrubber across the whole game: pick a move, watch its camera shot.
  This is presentation over the existing replay corpus (ARCH §11) — the engine already
  records move lists; M12 only draws them.
- A **highlight cut**: the first shot of a captured move zooms the capture cell, so a
  clip's thumbnail is the moment that mattered.

## M12.4 Authored camera presets (the M7/M9 tie-in)

Camera policy (M11.1) is data. M12 lets it be authored and shared:

- Extend the M7 variant document/serializer (`docs/plan/M7-editors.md` M7.0.2) with an
  optional `[camera]` block: a `CameraPolicy` per variant, canonicalised like every other
  authored field.
- **It is cosmetic, like `shape`/`height`:** it must **not** enter `VariantId`, and the
  M7.0.2 round-trip test asserts that editing camera policy leaves the id unchanged —
  exactly as editing a model or icon does.
- The preset travels through the M9 package and the Workshop, so an author can make their
  variant *look* the way they mean it to, and a viewer gets it for free.

## M12.5 Live spectator (the M8 tie-in)

- With M8's authoritative server, a spectator is a client that receives the move stream
  and runs the move camera, with no input. The camera is a pure function of the moves, so
  two spectators reconstruct the same shots — the same property that makes replay
  deterministic.
- No new protocol is needed: a spectator subscribes to the existing framed move stream.
  M12 only adds the presentation and the read-only client mode.

---

## M12.6 Play a game from notation (the marketing runner)

**Want.** Insert a pre-defined game — a list of moves — and have the app play it *automatically*,
move after move, so a marketing clip is the whole game rather than one hand-driven move.
"Insert a known game, hit record, get the same video every time."

**What it is.** A runner over the notation the engine already speaks
(`src/io/notation.hpp`, FEN-N in `src/io/fen.hpp`): a text file names the variant and an
optional starting position, then lists the moves. The runner loads it and plays each move in
order with the M11 move camera and the shape-follow choreography (M17.19), cinema-framed
(M12.1). It is a *pure function of the game and the timing* — no wall clock, no interaction —
so a clip of it is reproducible.

**Build.**

- **Input.** A game file: a small header naming the `variant` and optionally a `start` FEN-N,
  then one move per line in the engine's notation. Reuse the existing parser; do not invent a
  second notation. A malformed or illegal move **stops** at that move with a message naming it
  and the position, leaving the last legal move in place.
- **Player.** Advance one move per dwell: apply the move (which already starts the animation
  and the camera choreography), wait for the shot to settle, pause a settable dwell so the eye
  can follow, then the next. The runner lives in `src/app` (drives a `Session`), so it is
  headless and testable; the front end only forwards its clock.
- **Camera and pacing.** The existing settings choose the shot: `cameraMode`/`shapeFollow`,
  `followElevationDeg`, and the M17.19 choreography. A `--pace`/`--dwell` (seconds between
  moves) states the tempo for a clip; `--camera`/`--follow` pick the shot. Cinema mode
  (M12.1) drops the interface.
- **Determinism.** Like an overture, the runner reads no clock and no previous session: given
  (file, dwell, camera settings) it emits the same sequence of frames. `--play FILE` drives
  it interactively; `--clip DIR` over `--play FILE` writes the numbered frames.
- **Getting it in front of a user.** A "Watch a game" / "Demo" action built from the same
  runner, so a capture — and later the packaged demo (M16.3) — can play a canonical game with
  one command. The game files are plain content; the store trailer is a command plus a file.

**Tests.**

- A game file plays move-for-move; the final position and the move count match the file.
- An illegal/malformed move stops at that move with the right message, position unchanged past
  the last legal move.
- `--play FILE --clip DIR` is byte-identical on a second run.
- A game that wraps a glued seam / crosses a torus exercises the shape choreography,
  validation-clean.

**Acceptance.**

1. Given a notation file of N moves, the app plays all N automatically and reproducibly; a clip
   of the whole game is byte-identical on re-export.
2. The starting position can be stated (FEN-N), so a game can begin from a set-up diagram.
3. `standard`, a glued variant and a 4-D variant all play from the same file format (the
   variant is named in the header).

**Risks and non-goals.**

- **Not an analysis tool or an opponent.** The moves are given; there is no search (that is
  M10). It is a player of a script, nothing more.
- **Must not read the private narrative.** A marketing game is a plain notation file; the
  campaign/story stays in the private sibling repo (AGENTS.md's invariant).
- **The dwell and any ambient motion must stay out of the deterministic frame**, or clips stop
  reproducing — the M12.1 ambient-orbit rule again.

## M12.7 Save and load a game (the game saver)

**Want.** Save a played game to a file - the move history *and* the variant it was played on -
so it can be loaded and played again later, replayed through the move camera, or handed to the
M12.6 runner for a clip. "Save this, open it tomorrow, watch it again."

**What it is.** A self-contained game file: enough to reconstruct the exact position and the
whole move list, with no presentation state in it. It is the user-facing form of the replay
corpus (ARCH §11) and the input the M12.6 runner already wants.

**Build.**

- **Format.** A text document reusing what already exists: the variant **name** and its
  `VariantId` (so a rules change since the save is detected, not silently misapplied), an
  optional starting **FEN-N** (`src/io/fen.hpp`), and the move history in the engine's own
  notation (`src/io/notation.hpp`). The same parser the M12.6 runner uses reads it; a saved
  game is a playable notation file with a header.
- **Load.** Resolve the variant, load the start position, then replay the moves to rebuild the
  current position and history - so the game can be **continued**, **replayed** (M12.3) or
  **played back** by the M12.6 runner, all from one file.
- **Compatibility is explicit.** If the variant's `VariantId` differs from the save's, refuse
  with a message naming the mismatch rather than replaying a game the rules no longer describe.
  A load→save→load round-trip leaves the position hash and the move list unchanged (the same
  discipline as the M7.0.2 variant round-trip).
- **Where.** A saves directory beside settings (`Settings::defaultPath()`'s neighbourhood);
  **Save** / **Load** in the pause menu, and `--save FILE` / `--load FILE` on the command line.
- **Presentation is not saved.** Camera mode, pose, slide and theme are not in the file: a
  loaded game looks however the player's settings say, the way a `VariantId` never carries a
  model or an icon. It must **not** enter `VariantId`, and it must never carry narrative.

**Tests.**

- Save after N moves, load, and the position hash and move history match the saved position.
- Load→save→load is unchanged, and a replayed clip of the loaded game is byte-identical to the
  clip of the original.
- A file whose variant `VariantId` no longer matches is refused with a clear message.
- `standard`, a glued variant and a 4-D variant all save and load.

**Acceptance.**

1. A game is saved to a file and reopened later: the same position, the same move list, and
   the same replay.
2. The saved file plays through M12.6 with no conversion step.
3. A save made before a rules change is refused rather than misreplayed.

**Risks and non-goals.** Not a database or a gallery of games (that is M9 content); not a
multiplayer/network save (M8 owns the move stream); no engine or geometry change. The format
is data over the notation and FEN-N the engine already speaks, so the risk is the compatibility
check being skipped - which is exactly what the `VariantId` test pins.

## Tests and acceptance

- **Clip determinism:** the same clip arguments produce byte-identical frame files on a
  second run, and the frames are validation-layer clean (the existing `--shot` gate).
- **Cinema chrome:** a cinema-mode capture contains no rail/ledger/highlight pixels —
  asserted by comparing against a capture with an impossible highlight cell that must
  leave no trace.
- **Ambient orbit is not captured:** a clip taken at two different wall-clock times is
  identical, proving the ambient orbit is excluded from the deterministic shot.
- **Preset round-trip:** a variant's `[camera]` block survives load → save → load, and
  changing it does not change `VariantId`.
- **Replay:** replaying the last move twice yields the same frames; the history scrubber
  reaches every recorded move.

### Acceptance facts

1. A move is exported as a clip on `klein`, `cube5` and `t6` with no display and no
   validation messages, reproducibly.
2. Cinema mode shows the board and the move with no interface furniture.
3. Camera policy authored into a variant round-trips, ships through a package, and does
   not alter `VariantId`.
4. A spectator reconstructs the same camera shots as the player, from the move stream
   alone (with M8).

## Risks and non-goals

- **Encoding is the dependency risk.** Keep the guaranteed output a frame sequence; treat
  a muxed video as a convenience, never as a required build dependency. No codec in the
  deterministic core.
- **The ambient orbit must stay out of the deterministic path**, or clips stop being
  reproducible. This is the M11 "overture is a pure function of `t`" rule applied again.
- **Non-goals:** no in-engine video editor, no transitions library, no live-streaming
  integration, no protocol change (M8 owns the protocol; M12 is a read-only client), no
  engine or geometry change of any kind.

## Status

Planned, not started. Depends on M11 (hard), M9 and M8 (for the sharing and live halves).
The frame-sequence exporter and the `[camera]` document extension are the two pieces that
touch existing code; everything else is presentation over seams that already exist.

### As built (2026-09-30)

M12.1 cinema mode and M12.2 clip export are in: `--cinema` fills the frame with the board
and records no interface; `--follow off|piece|route` and `--move-t 0..1` show the move
camera in a capture; `--clip DIR --frames N --t0 a --t1 b` renders a numbered PPM sequence
deterministically - a clip frame is byte-identical to the `--shot` at the same `t` - and
muxing stays external.

**Deferred to Wave 4:** camera presets (M12.4, needs M9) and live spectating (M12.5, needs
M8). The `[camera]` document extension rides M12.4.

**Planned, Wave 1 (2026-10-01): M12.6, play a game from notation.** The marketing runner:
insert a pre-defined game as a notation file and have it play itself — every move, in order,
through the move camera and the M17.19 shape choreography — so a clip is the whole game and a
trailer is a file plus a command. It shares the deterministic contract with cinema and clip
export, so `--play FILE --clip DIR` reproduces byte-for-byte.
