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
