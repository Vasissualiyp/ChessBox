# M18 — Production polish: the game stops reading as a tech demo

Spec'd 2026-10-04, after Wave 1's visuals (M11/M12/M13/M17) landed. Prompted by a direct
note during a playtest: the engine is sound and the shape view is a genuine trailer moment,
but the moment-to-moment feel - a capture that vanishes with no acknowledgement, pieces with
no contact shadow, total silence, a flat-colour void behind the board, raw unframed footage
out of the M12.6 runner - reads as a tech demo rather than a shipped game. None of this is
new engine work: it is finishing the presentation layer around engine work that is already
done.

**Not a release-sequence wave.** This sits beside Wave 1/2 rather than inside them - every
item here is presentation, addressable independently, and ships whenever it is ready. Several
items revisit M4.7's deliberate M4-time scope boundary ("no shadows, no post-processing");
that boundary was correct for M4 (a renderer not yet proven should not carry speculative
weight) and is being revisited now because the goal has changed - M4 needed a board that
worked, M18 needs footage that sells.

**Order, cheapest/highest-impact first:**

1. **M18.1 Capture/move juice** - a capture currently just disappears.
2. **M18.2 Contact shadows** - pieces read as floating.
3. **M18.3 Cinema vignette and grade** - exported clips are unframed and flat.
4. **M18.4 The ambient background, redone** - the existing decoration is colourful confetti
   with no connection to the game; replace it with the game's own geometry, drawn quietly,
   and bring a toned-down version to the board itself.
5. **M18.5 Audio v1** - the game is silent. `Settings` already has volume sliders stored
   "for when it does" (`src/app/settings.hpp`). Biggest single perception change, biggest
   lift; scoped so a v1 needs no licensed assets at all.
6. **M18.6 The flat/shape morph, and view controls that cannot contradict each other** -
   SHAPE currently snaps instead of morphing (the overture already does this exact morph,
   one layer over - `PlaySurface` just never asks for it), and 2D/SHAPE can be set to states
   that make no sense together.

## M18.1 Capture/move juice

**Want.** A capture is the single most dramatic moment chess has, and right now it is
invisible: the captured piece's instance simply stops being drawn once the position updates,
with no transition and no cell-level acknowledgement. A marketing clip of a capture should
look like one.

**What it is.** A short, local flourish at the captured cell, timed against the existing move
animation rather than a new clock.

**Build.**

- **Locate the hook.** `view::MoveAnimation` already distinguishes `Move::captureCell` from
  `Move::to` (see AGENTS.md's gotcha: "`Move::captureCell` is not `Move::to`" - en passant is
  the one case they differ). Find where `BoardRenderer::buildInstances` currently decides
  whether to include the captured piece's instance during the animation (it is presumably
  drawn normally until the move resolves, then dropped abruptly) and where the capturing
  piece's own arrival is timed.
- **The captured piece fades, it does not vanish.** Scale and fade its instance out over a
  short window timed to the move's own progress (reuse the easing helpers already used for
  the move camera's shot envelope, e.g. `view::shotEnvelope`-style smoothstep, not a new curve
  vocabulary), ending at or just before the capturing piece's arrival.
- **A flash ring at the cell.** One short-lived instance - reuse `Archetype::Cell` (the flat
  slab already used for the board itself) scaled and coloured, or add a new thin-ring
  archetype if `Cell` cannot be made to read as a ring - that brightens at the capture instant
  and fades over a few hundred milliseconds. Colour: `Theme::blood` already exists
  ("semantic, deliberately separate from the accent" - used today only for the check
  vignette) is the natural choice; a capture and a check sharing one "something happened"
  hue is consistent rather than introducing a second meaning for red.
- **Determinism.** Like every other piece of M11/M12/M13, this must be a pure function of
  the move's own progress `t`, not of wall-clock time or frame count, so `--clip` stays
  reproducible. No new per-frame state beyond what `MoveAnimation` already carries.

**Tests.**

- A unit/property test that the captured piece's instance alpha is 1 before the capture
  window and 0 after it, monotonic in between, as a pure function of `t`.
- A validation-clean `--shot`/`--clip` of a capturing move on `standard` at a `t` inside the
  flourish window, and a differential check that the same `t` twice produces the same frame
  (determinism).
- A test that a *non*-capturing move triggers no flourish instance at all.

**Acceptance.** A capture, watched in `--cinema` at normal pace, reads as an event - the
captured piece visibly leaves rather than disappearing between frames - and a `--clip` of it
is still byte-identical on re-export.

**Risks and non-goals.** Not a combat system or a damage number; one flourish, tuned for
legibility at trailer resolution, not a cosmetics system with options. Must not change
`Move`/`MoveAnimation`'s timing contract that the move camera (M11) already depends on - this
is a draw-time decoration over an unchanged animation, not a longer move.

### Status: M18.1 built (2026-10-04, opencode)

A capture now leaves a flourish: the captured piece shrinks and fades out over the move,
and a blood flash pulses at the square it stood on as the pieces meet.

- **Where it hooks.** The spec guessed the captured piece "is drawn normally until the move
  resolves, then dropped". It is not: `Session::playChecked` applies the move and calls
  `refreshSnapshot` *before* `MoveAnimation::start`, so the captured piece is already absent
  from the `PositionView` the renderer is handed. `view::tracePath` (which already runs
  against the pre-move position) now records `MovePath::captureCell` and a copy of the
  captured `Piece`; `MoveAnimation::start` keeps them and exposes `captures()`,
  `captureCell()`, `capturedPiece()`. `BoardRenderer::buildInstances` rebuilds the piece
  from that copy. `captureCell` is used, never `to`, so en passant fades the pawn on d5, not
  the empty square the capturer lands on (AGENTS.md's gotcha).
- **Pure function of `t`.** `view::captureFade(progress)` returns 1 through the first fifth
  of the move and falls smoothly to 0 by four fifths of the way in (before the capturing
  piece arrives); `view::captureFlash(progress)` is a smoothstep pulse peaking at `t = 0.8`.
  Both live in `src/view/move_anim.cpp` and reuse the `shotEnvelope`-style smoothstep, with
  no new curve vocabulary and no state beyond `MoveAnimation`'s own progress.
- **The blended pass.** The opaque pipeline writes alpha but does not blend, so the flourish
  is kept in a new `InstanceSet::flourish`/`flourishBatches` and drawn as one extra group
  with the existing GHOST `surfaceBlendPipeline_` (blend on, depth write off, depth test on)
  - no new pipeline, no second buffer; the flourish instances ride the instance buffer
  immediately after the opaque ones. The flash is `Archetype::Cell` scaled and grown as it
  fades, coloured `Theme::blood` (the check-vignette hue), as the spec allows. Both the flat
  board and the geometry view emit the flourish; flat (`options_.flat`, UI-drawn pieces) is
  skipped.
- **Tests.** `[view]` unit tests pin `captureFade`/`captureFlash` boundaries and
  monotonicity and that the trace/animation carry the captured piece (including en passant);
  a no-GPU `[render]` test builds instances for a real `standard` capture and checks one
  opaque captured piece at `t = 0`, a translucent piece plus a blood flash mid-window,
  byte-identical `flourish` on a repeat build, an empty flourish at `t = 1`, and an empty
  flourish for a non-capturing move; a second no-GPU test does the same for `torus` with
  `options.surface` on, so the geometry view's own copy of the flourish is covered.
- **Deviations.** (1) The captured piece had to be carried on `MovePath`/`MoveAnimation`
  rather than read from a snapshot that no longer has it. (2) The fade is drawn through the
  existing GHOST blend pipeline rather than by adding blending to the opaque one or a new
  pipeline. (3) The flash is the reusable cell slab, not a new ring archetype (the spec's
  cheaper option).
- **Verification.** `tools/test.sh --build view render` green; `tools/precommit.sh` green
  (format, build, arch, unit, property). Visually validated with
  `standard --cinema --move-t T --script` (e2-e4, d7-d5, exd5) rendered to
  `build/m181_check/`: at `t = 0` the black pawn sits whole on d5, at `t = 0.45` it is
  shrunk and translucent against the arriving white pawn, at `t = 0.8` a red flash covers
  the square, at `t = 0.95` both are spent. Two captures at the same `t` are byte-identical
  (`cmp`), and every capture is validation-clean.

## M18.2 Contact shadows

**Want.** Every capture taken during Wave 1's QA pass shows pieces that read as floating a
few millimetres above their square - there is no cue tying a piece to the cell under it. A
contact shadow is one of the cheapest "looks finished" tricks there is.

**What it is.** A soft, dark, roughly circular decal under each piece, drawn before the piece
in the same instanced pass the board already uses - not a shadow-mapped light, which M4.7
correctly scoped out and which this does not need.

**Build.**

- **One instance per occupied cell**, positioned at the cell's own surface point (the flat
  board's cell centre, or - on the geometry view - the seat's `centre` from `PlaySurface`,
  nudged along the normal by a small fraction of a cell so it never z-fights the board),
  scaled smaller than the piece's own footprint, coloured dark and translucent
  (`Theme::soot`/`ink` at low alpha - check both themes render it correctly, the way
  `tests/unit/view/test_theme.cpp` already pins piece-on-square contrast).
- **Shape.** `Archetype::Cell` is a flat slab, not a disc with a soft radial falloff; a hard-
  edged square shadow will look wrong. Either add a small fragment-shader radial falloff to
  the existing flat-slab path gated by a new per-instance flag (cheapest, reuses the existing
  pipeline and shader), or add a new `Archetype` (a low-poly disc) if a shader branch is
  judged worse than one more mesh. Decide by trying the shader-flag route first - it is less
  code and this project's own convention is "one material, no shadows, no post-processing"
  stays mostly true if this rides the existing pipeline rather than opening a second one
  (contrast with GHOST and M17.21's rails, which both deliberately stayed inside the existing
  per-vertex-colour / existing-pipeline discipline rather than adding passes).
- **The shape view.** A piece's shadow there should sit on the surface under it, using the
  seat's own normal - reuse exactly the placement math `kSurfaceHoverCells`/the piece's own
  seat placement already computes, so the shadow is "one cell's worth of offset along the
  normal" the same way the GHOST/rail work already thinks about placement on a curved board.
- **Does not apply during a glide's hover.** A piece mid-glide is already hovered off the
  surface (`kSurfaceHoverCells`) by design - its shadow should stay pinned to the board at
  the position directly below the hovering piece (projected along the normal), not follow the
  piece up, or the "contact" the shadow exists to show becomes a lie.

**Tests.**

- A render test that an occupied cell gets exactly one shadow instance and an empty cell
  gets none.
- `tests/unit/view/test_theme.cpp`-style contrast check: the shadow must not make a dark
  piece unreadable on a dark square in either theme (measure, do not eyeball).
- Validation-clean `--shot` on `standard` (flat) and `torus` (geometry view) with shadows on.

**Acceptance.** Every occupied cell, flat or on a shape, shows a soft dark decal under its
piece; `standard --shot` before/after is visibly different only by the addition of shadows
(no other instance, colour or layout change).

**Risks and non-goals.** Not real shadow-mapping, not self-shadowing between pieces, not
affected by the (nonexistent) light's position - a flat, cheap, always-on ground contact cue,
nothing more. If the shader-flag route turns out to need real branching complexity, fall back
to a second thin disc mesh rather than fighting the single-material constraint - record
whichever is chosen and why, the way GHOST's status note already records its own pipeline
decision.

## M18.3 Cinema vignette and grade

**Want.** `--cinema` strips the interface so the board is the whole frame, but the frame
itself is undressed - flat lighting, square corners, no sense that a camera is "shooting" it.
A subtle vignette and a small contrast/saturation push, applied only in cinema mode, costs
nothing structurally and reads immediately as "graded footage" rather than "a screenshot."

**What it is.** A 2-D overlay, exactly like the existing check-warning vignette
(`Ui::drawCheckEdges`, `src/render/ui.cpp`: "a soft red vignette at the frame's edge: a few
nested outlines, strongest at the very edge and fading inward") - reuse that technique
directly, neutral-toned instead of red, gated on cinema mode instead of check.

**Build.**

- **Vignette.** Copy `drawCheckEdges`'s nested-outline technique (same ring count, same
  falloff shape is a fine starting point) with `theme.ink`/`theme.soot` instead of
  `theme.blood`, gated on a new `cinema` flag reaching `Ui::build` (cinema mode is already a
  capture-path and interactive concept - `--cinema` on the CLI, and presumably a settings or
  session flag for the interactive "watch" use case M12.6 adds; thread whichever already
  carries that intent, do not add a second one).
- **Grade.** A small, fixed contrast/saturation adjustment is harder to do as a 2-D overlay
  trick (multiplying over colour does not raise contrast the way a real curve does) - the
  cheapest honest version is a very low-alpha black overlay for lift/crush plus a very
  low-alpha saturated-colour overlay (e.g. a faint warm or cool wash, picked from the theme,
  not an arbitrary new colour) for a touch of colour grading, both 2-D overlay tricks like the
  vignette - no shader, no post-processing pass. If this reads as too subtle or too crude in
  practice, the more correct version is a real colour-curve post-pass, which is a materially
  bigger lift (a second render target and a fullscreen pass) - try the overlay version first
  and only escalate if it visibly fails.
- **Off by default outside cinema.** Ordinary play must look exactly as it does today; this
  is additive and gated, never a change to the default theme.

**Tests.**

- A pixel-level test: a cinema-mode capture's corner pixels are measurably darker than its
  centre pixels (the vignette), and a non-cinema capture of the same position is unaffected.
- Validation-clean `--shot --cinema` on a couple of variants.

**Acceptance.** `--cinema` output visibly reads as graded footage next to an ungraded
capture of the same frame; interactive play and non-cinema captures are pixel-identical to
before this lands.

**Risks and non-goals.** Not colour management, not HDR, not a settings-exposed grading
system - one fixed look, tuned once, for the one purpose (marketing capture) cinema mode
exists for. If it fights `--ghost`'s own alpha blending or M17.21's rails anywhere, that is a
compositing-order bug to fix, not a reason to change either feature.

## M18.4 The ambient background, redone

**Want.** The existing menu background (`render::DepthField`, `src/render/deco.cpp`) is 120
bodies of randomly-coloured pastel polygons plus a few piece icons, cycling hue continuously -
confetti, disconnected from what the game actually is. Direction from the same playtest
conversation: replace the abstract polygons with the game's own geometries (torus, Klein
bottle, Möbius strip, cube, hypercube - **not** the quintic/`t6`, which is "too much" visually
busy for a background), drawn as thin wireframes rather than filled shapes, in a muted
"near-future, string-theory" palette rather than rainbow pastels, and bring a toned-down
version to the actual game screen, which currently has no background decoration at all.

**What it already has to build on.** This is cheaper than it sounds: the whole decoration
system (`deco.cpp`) is 2-D ImGui draw-list math with its own small local camera/projection
helpers (`Camera`, `rotateX`/`rotateY`) - it does not touch the Vulkan renderer, the instanced
pipeline, or `BoardRenderer` at all. `drawTesseract` *already is* a muted wireframe 4-cube
(theme-toned `line` colour, edges drawn with `AddLine`, corners as small filled squares) -
the exact technique this item needs, for one of the five shapes already. `Theme` already has
an unused-for-this-purpose "cold" accent (`rift`/`riftDeep`, cyan, "reserved") and the seam
hue ramp (`seamHueBegin/End/Saturation/Value`, "a cold arc...that deliberately never reaches
the warm half of the wheel, which belongs to the game rather than to the geometry") - both
are exactly the muted, thematically-load-bearing palette this item should draw from instead
of `kPastels`/`kPastelsDark`.

**Build.**

- **Four more wireframe shape-drawers, alongside `drawTesseract`.** A torus (a ring of
  wireframe quads or just lines along its two parameter directions - reuse the parametric
  idea from `shellTube`/the overture torus, projected through the same local `Camera` struct
  `drawManifold`/`drawTesseract` already use, not the full 3-D renderer), a Klein bottle
  (reuse `kleinSurf`'s parametrisation the same way, wireframe not filled), a Möbius strip
  (a degenerate case of the same family, or its own simple parametrisation), and a cube (the
  easy one - `drawTesseract`'s 4-cube code specialised to 3 axes, or simply drawn by hand,
  it is eight points and twelve edges). Each one slowly rotating, line-only, no fills, in the
  `rift`/seam-ramp palette.
- **`DepthField`'s body vocabulary changes.** Today a body is either a piece icon or a
  random-sided filled polygon (`kPastels`-coloured). Replace the polygon kind with "one of
  the five wireframe shapes" (keep the piece icons - they are already "the game appearing in
  its own background," which is the right idea and not what was called out as a problem).
  Fewer bodies than today's 120 is likely right, since a wireframe shape reads as more
  complex than a triangle at a glance - tune by eye, but expect a smaller number with more
  visual weight each, not the same count.
- **Colour.** Replace `kPastels`/`kPastelsDark` sampling for these bodies with the seam hue
  ramp (`view::seamRampColor`-style, or `Theme::rift`/`riftDeep` directly) at low saturation/
  low alpha - "wireframe-y," glowing faintly, never competing with the board the way the
  existing rule for seam colour already states ("saturated colour is reserved for geometry
  that is not flat... the shell itself is cool and neutral"). This item is squarely an
  application of that existing rule, not an exception to it.
- **The main menu's dedicated object.** `decoForScreen` currently gives `MainMenu`/`Welcome`
  the quintic-cross-section `Deco::Manifold` object (`drawManifold`) *in addition to* the
  `DepthField` background every shell screen gets. Per the "quintic is too much" direction,
  retire `Deco::Manifold` for the main menu specifically (fold to `Deco::None` there, letting
  the redone `DepthField` alone carry the background) rather than keeping two decoration
  systems stacked. `Deco::Manifold` itself need not be deleted if the quintic is still wanted
  elsewhere (e.g. `t6`'s own overture, which is unrelated and out of scope here) - just stop
  selecting it for `MainMenu`/`Welcome`.
- **Bringing it to the game screen.** `Ui::drawShellFrame` (where `field_.draw(...)` is
  called today) is not currently invoked for `Screen::Game` - the board HUD
  (`Ui::buildGameHud`) draws the board and its labels directly with no background decoration
  at all. Add a *much* quieter call into the same `DepthField` - fewer bodies, lower alpha,
  likely wireframes only (no piece icons, which would visually compete with the real pieces
  on the board) - behind the board. This must never distract during play: start it low-alpha,
  get a second opinion (literally - show a capture to the user) before tuning it brighter.
- **Determinism.** `DepthField` already documents itself as reading no clock it cannot be
  given ("the shell has to draw the same frame twice... so nothing here may reach for a clock
  or a global random source") - keep that invariant for the new shapes; they are driven by
  the same `t`/seed the existing bodies already use.

**Tests.**

- A render test that each of the five wireframe shapes draws a non-degenerate, non-empty set
  of line segments at a few sample `t` values (a smoke test, not a pixel-perfect golden - this
  is 2-D decoration, not board state).
- A differential/consistency test that the background is identical across two calls with the
  same `t`/seed (determinism, same discipline as every other animation in this codebase).
- A validation-clean `--shot --screen menu` and a validation-clean `--shot` of an ordinary
  game screen with the new quiet background on.
- A contrast/legibility check: with the game-screen background on, the existing piece/board
  theme contrast test (`tests/unit/view/test_theme.cpp`) must still pass - the ambient
  background must not erode the thing that test protects.

**Acceptance.**

1. The main menu's background reads as "the game's own exotic geometries, quietly" rather
   than "rainbow confetti" - torus, Klein bottle, Möbius strip, cube and hypercube wireframes
   drifting through the depth field, coloured from the existing cold/seam palette.
2. The quintic is no longer the main menu's dedicated object.
3. The game screen has a subtle version of the same background, which a player can look at
   for a full game without it becoming distracting (this is a judgement call - get the user's
   sign-off on the tuning before calling it done, the same way GHOST/the chase camera needed
   several rounds of "it still looks wrong" before landing).
4. `tools/test.sh --build render` and the app-tagged suite stay green; the theme contrast
   test is unaffected.

**Risks and non-goals.** Not a new rendering subsystem - everything here stays inside
`deco.cpp`'s existing 2-D draw-list approach, deliberately, since that is what makes it cheap.
Not a per-variant background (every screen gets the same five-shape vocabulary regardless of
what is being played - a `t6` game does not get a louder background than a `standard` one).
If the game-screen version cannot be tuned subtle enough to avoid distracting during real
play, ship it cinema-mode-only (or capture-only) rather than shipping something that hurts the
moment-to-moment experience for the sake of a background.

## M18.5 Audio v1

**Want.** The game is completely silent. `Settings` already carries `volumeMaster`/
`volumeMusic`/`volumeEffects` with the comment "the game has no sound yet; these are kept for
when it does" (`src/app/settings.hpp`) - the intent was always there, nothing was ever built.
This is very likely the single biggest "tech demo" tell of everything in this document, and
also the biggest lift - scoped here as a v1 that needs **no licensed or sourced assets at
all**, so it can ship without anyone's input beyond this spec.

**What it is.** A small audio layer using SDL3's own audio API (already a hard dependency -
`sdl3` is already in `flake.nix`; no new library), playing a handful of procedurally
synthesised sound effects at the moments that already exist as discrete engine events (a
move lands, a capture resolves, a UI action fires) - no music in v1, since a good loop needs
an actual composed/sourced asset and licensing judgement that belongs to the user, not to a
procedural generator.

**Build.**

- **New layer, `src/audio` (new CMake target, per `cb-new-module`), sitting beside `render` in
  the layer map** (it depends on nothing below `base`, and nothing above it depends on it
  except `app`/`gui` - update `AGENTS.md`'s layer table in the same commit per the repo's own
  rule 8). Uses `SDL_OpenAudioDeviceStream`/`SDL_PutAudioStreamData` (SDL3's audio API) to
  play short PCM buffers on demand; no streaming, no file I/O, no codec.
- **Procedural SFX, generated once at startup into static buffers**, not shipped as asset
  files: a handful of short (<200 ms) envelope-shaped tones/noise bursts - a soft click for
  UI navigation, a slightly longer tone for a piece landing, a sharper/lower one for a
  capture (pairs naturally with M18.1's visual flourish - trigger both from the same event),
  a distinct one for check. Simple additive sine/noise synthesis with an ADSR-style envelope
  is enough; this is deliberately not sound design, it is "something plays instead of
  nothing," tuned for clarity over character. Keeps this v1 entirely free of licensing
  questions and asset-pipeline work, consistent with the engine's existing "no magic assets"
  discipline (`src/assets`'s own pieces are integer-vector data, never imported models).
- **Hook points**, each a one-line call into the new layer from code that already knows the
  event happened - do not invent new event plumbing:
  - A move lands / a capture resolves: `Session`/`Game`'s existing move-application path.
  - A UI navigation action (`Ui`'s button press handling) - reuse whatever single chokepoint
    already exists for "a button was activated," if one does; add one if every button handles
    its own click independently today.
  - Check: wherever `shell.showsCheckWarning()` (already driving the visual vignette,
    `drawCheckEdges`) becomes newly true, not every frame it stays true.
  - The MainMenu/NewGame deco field is wisely left **alone** here - no ambient audio loop in
    v1, since that is squarely "music," out of scope.
- **Mixing.** `volumeMaster * volumeEffects` scales every SFX; the stored `volumeMusic`
  setting is read but has nothing to multiply yet (no music in v1) - leave it wired through
  so turning it on later is a one-line change, not a second settings pass.
- **Respect the settings screen's own honesty.** `ui_menus.cpp` line ~1070 currently shows
  "the game has no sound yet; these are kept for when it does" - remove that line (or narrow
  it to "no music yet," if music genuinely stays out) once SFX actually play, so the settings
  screen does not contradict what just shipped.
- **Headless/capture safety.** `--shot`/`--clip`/any headless path must not require an audio
  device - SDL3's audio init must degrade gracefully (log and continue silent) when no device
  is available, exactly the pattern the renderer already uses for a missing font ("a missing
  font is never fatal - the interface falls back and looks plainer").

**Tests.**

- A unit test that the synthesis functions produce non-silent, non-clipping, correctly-sized
  PCM buffers for each SFX (deterministic - same inputs, same samples, pinned the way every
  other generated-content path in this codebase is).
- A test that `volumeMaster = 0` (or `volumeEffects = 0`) produces silence (every sample
  zero, or the play call is skipped outright) rather than merely a quieter buffer, since a
  player expects "off" to mean off.
- A headless-path test (or an explicit assertion in the existing `--shot`/`--clip` tests) that
  capture commands still succeed with no audio device present (CI has none).
- Manual/listening verification is unavoidably part of accepting this one - note in the
  status section whether it was actually listened to, not just unit-tested.

**Acceptance.**

1. A move, a capture, a check and a UI navigation action each produce a short, distinct sound
   in normal interactive play.
2. The volume sliders actually do something; `volumeMaster`/`volumeEffects` at 0 is silent.
3. No asset files are added; everything audible in v1 is generated in code.
4. `--shot`/`--clip`/headless tests are unaffected by audio being present.
5. The settings screen's "no sound yet" note is updated to match reality.

**Risks and non-goals.** Not music - a v1 with no loop is an accepted, explicit scope cut,
not an oversight; music is a follow-up once an actual asset is sourced or composed and its
licence is confirmed, which is the user's call, not code. Not spatial/3-D audio - stereo or
mono SFX triggered by event, no positional mixing tied to camera/board position. Not a sound
designer's pass - procedural placeholder tones are explicitly "good enough to not be silence,"
and a follow-up with authored sounds (once sourced) replacing the procedural ones is expected
and fine; record that distinction in the status note so a later pass knows which sounds are
placeholders.

## M18.6 A morph between the flat board and its shape, and view controls that cannot
## contradict each other

**Want.** Two things, found while playing with the SHAPE toggle (M17): switching it snaps
instantly between the flat board and the fully-formed shape, where everywhere else a
transition like this (a screen change, a variant selection in the library) morphs - that
inconsistency is exactly the kind of rough edge that reads as unfinished. Separately, the 2D/
3D button and the SHAPE button can be set to states that make no sense together: SHAPE is
offered while the board is flat 2D (a donut has no 2D rendering - there is nothing to show),
and switching to 2D while SHAPE is on silently does *something* rather than being refused.

**It already exists, one layer over.** The library/menu overture
(`render::derivedOvertureScene`/the hand-authored scenes, `src/render/overture_scene.cpp`)
already does precisely this morph: "pure in `t`...it opens on the flat board, forms the
surface the variant's identifications imply, and unwinds." The underlying surface functions
(`kleinSurf`, `shellTube`, and their siblings) take continuous roll parameters (`th`, `ph`)
where 0 is the flat board and the shipped value is fully formed - "the flat box falls out of
it as `th = ph = 0`" (`shellTube`'s own comment). `PlaySurface` - the *play* board's version
of the same shapes - calls these at the fully-formed value **always**;
`render::SurfacePose`'s own doc comment says so explicitly: "The surface functions still
*carry* a geometric eversion - the overtures use it - but the play board does not take it."
So the morph this item wants is not new math, it is **exposing a knob `PlaySurface` already
has access to but never uses**.

**Build.**

- **Add a `formed` field to `SurfacePose`** (`src/render/overture_scene.hpp`), 0..1, default
  1 (so every existing caller that does not set it keeps today's always-fully-formed
  behaviour - `nix flake check`'s goldens must not move). Thread it into `PlaySurface::build`/
  `buildStacked`'s calls to the underlying surface functions as the `th`/`ph` (or equivalent)
  scale, exactly the way `pose.openness`/`pose.twist` are already threaded through today -
  this is adding one more pose component to a mechanism that already passes several.
- **A target the front end eases toward**, exactly `Settings::geometryEvert`'s own pattern
  ("the `INVERT` button... sets a **target** the front end eases... to, so the pose stays a
  pure function of its number"): a new `Settings::geometryFormed` (default 1) is the live,
  eased value actually fed to `SurfacePose::formed`; toggling SHAPE sets a target (1 to show
  the shape, 0 to return to flat) and the interactive loop eases the live value toward it over
  a fixed duration - reuse whichever easing helper `geometryEvert`'s own transition already
  uses rather than inventing a second one. Still a pure function of a number, still
  `--shot`-able mid-transition (state `--formed 0.5` the way `--evert`/`--slide` already can
  be stated for a capture).
- **What "flat" looks like at `formed = 0`.** This needs an actual decision, not just a
  number: does the board at `formed = 0` look like the *ordinary flat board* (cells at their
  lattice positions, same as `flatView`/non-surface rendering), or like the overture's own
  "opens on the flat board" pose (which may differ slightly - check `shellTube`/`kleinSurf`'s
  `th = ph = 0` output against the ordinary flat-board layout before assuming they coincide).
  If they do not naturally coincide, the honest fix is adjusting the surface function's
  `formed = 0` case to match the real flat board exactly (a torus's `th=0` state should **be**
  the ordinary 8x8 grid, not merely resemble it), since the morph has to land exactly on the
  thing SHAPE-off already draws, or toggling twice would leave the board in a visibly
  different place than where it started - pin this with a test, not an eyeball check.
- **Mutual exclusivity (the simpler half).** `widgets::button` already takes an `enabled`
  parameter - every call site in `src/render/ui.cpp`'s view-control row currently passes a
  hardcoded `true` for it. Change two call sites (~line 1274 and ~line 1286 at the time of
  writing - grep for the `"2D"`/`"3D"` and `"SHAPE"` button calls, they sit beside each
  other): the 2D/3D button's `enabled` becomes `!shell.settings().geometryView` (greyed out
  while the shape is showing - the player sees why: the state that would conflict is right
  there, lit), and the SHAPE button's `enabled` becomes `!session.flatView()` (greyed out in
  2D, since there is nothing to show). Do **not** hide either button - a control that
  disappears is a worse surprise than one that is visibly present but refuses a click; keep
  both visible, just non-interactive, matching how `button`'s existing `enabled=false` state
  already renders elsewhere in the interface.
- **What happens to an in-flight morph if the now-disabled control is attempted anyway?**
  It cannot be, by construction, once the buttons are gated - but a scripted `--script`
  capture or a settings-file edit could still set both `flatView` and `geometryView` true at
  once. Decide and document one resolution (flat wins, since a shape has no 2D rendering to
  fall back to) rather than leaving the combination undefined; a short test should pin it.

**Tests.**

- A property test that `PlaySurface` at `SurfacePose::formed = 0` produces the same cell
  positions as the ordinary flat (non-surface) board layout, for `torus`/`klein`/`mobius` -
  the "lands exactly on what SHAPE-off already draws" guarantee above, pinned.
- A property test that `formed = 1` is pixel/position-identical to today's always-fully-
  formed output (the existing shape-view goldens must not move - this is the backward-
  compatibility check for the new default).
- A test that `--formed 0.5` (or whatever the capture flag ends up named) produces a
  validation-clean, in-between frame - the shape recognisably mid-morph, not a half-built
  mesh or a crash.
- A UI test (or a documented manual check, matching how other `widgets::button` `enabled`
  states are verified elsewhere) that the 2D/3D button is disabled exactly when
  `geometryView` is on, and SHAPE is disabled exactly when `flatView` is on.
- A test pinning the `flatView && geometryView` resolution decided above.

**Acceptance.**

1. Pressing SHAPE morphs the board from flat to its shape over a short, visible transition
   instead of snapping; pressing it again morphs back, landing exactly on the ordinary flat
   board.
2. The 2D/3D button is greyed out while SHAPE is on; the SHAPE button is greyed out in 2D -
   neither can be used to reach a contradictory state.
3. Every existing shape-view and flat-view golden/capture is unchanged (the new default of
   `formed = 1` reproduces today's output exactly).
4. `tools/test.sh --build render` and `--build app` green.

**Risks and non-goals.** Not a new animation system - this reuses the overture's own roll
math and the `geometryEvert`-style eased-target pattern, both already established. Not every
pose control needs the same morph treatment (slide/invert/ghost can stay as they are unless
a later note says otherwise) - scope this to the flat<->shape transition and the two buttons
named above. If `formed = 0` turns out not to coincide with the flat board for some shape
after investigation, fixing that is in scope (it is load-bearing for acceptance criterion 1),
but changing the *shipped, fully-formed* shape's appearance is not - `formed = 1` must stay
pixel-identical to today.

## Dependencies and ordering

M18.1-M18.3 and M18.6 touch only `src/render`/`src/view`/`src/gui` and are independent of
each other and of M18.4/M18.5 - any order, any subset. M18.4 is independent of everything
else in this file. M18.5 is a new layer and the most self-contained of the six (new module,
event hooks, no shared state with the others) but is also the largest - do it last, or in
parallel with the others if capacity allows, since nothing here blocks on it.

## Risks and non-goals (the whole milestone)

- **This is not a reopening of M4.7's scope boundary in general** - "no shadows, no post-
  processing" stays the right default for the renderer's hot path; every item here is either
  a 2-D overlay trick (M18.3), an extension of the existing instanced/vertex-colour mechanism
  GHOST and M17.21 already proved out (M18.1/M18.2), pure 2-D draw-list decoration that never
  touches the Vulkan pipeline (M18.4), or a new, separate, optional layer (M18.5). None of it
  is "add a deferred renderer" or "add PBR materials."
- **Subjective tuning is expected to take rounds.** Several items here (shadow strength, the
  cinema grade, the ambient background's opacity and body count, the SFX envelopes) are
  judgement calls that will need the user's eye, not a test assertion, to call "right" - plan
  for iteration, the way the chase camera's elevation/width/twist settings each needed several
  passes in M17.
- **Do not let polish regress determinism.** Every item above restates it, deliberately:
  `--shot`/`--clip` byte-identical reproduction is the thing the whole Wave 1 marketing
  pipeline depends on, and it is far easier to break by accident while adding "juice" than
  while adding a camera.
