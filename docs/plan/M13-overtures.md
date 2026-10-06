# M13 — Generic Variant Overtures

**Goal.** Every variant animates on the library screen — including one that did not exist
when the build was made. Today the New Game animation is a closed compile-time set:
`app::Overture` is an enum (`src/app/overture.hpp:15`) and `overtureScene` is a `switch`
over it into hand-built per-variant scene functions (`src/render/overture_scene.cpp`,
75 KB). That is the right way to make *fourteen* scenes stunning and the wrong way to make
the *thousandth*. M13 adds a **derived** overture, built from the variant's own
`VariantSpec`, and keeps the hand-authored scenes as overrides for the marquee variants.
It is the content-scale answer that makes M9 (Workshop) work: a user-authored variant
cannot be added to a compile-time enum.

**Exit condition.** A variant that exists only as a data file — never named in the C++ —
animates on the library screen: it opens on the flat board, forms the surface its geometry
implies, shows one of its own pieces making one of its own wrapped moves, and unwinds. No
scene function is written for it, and its seam closure is asserted by the same property
test the hand-authored surfaces pass.

---

## The one idea: the overture is a function of the spec, the way the camera is a function of the move

M11 generalised the camera by observing that it only needs `MovePath` (topology as tags),
`layout` (dimension as world positions) and a policy record. M13 does the same for the
overture: a scene is three derived things and one authored narrative.

1. **The surface is derived from the identifications.** The variant already declares
   which axes are periodic, which are mirrored, and where the twists are (ARCH §4.1). A
   *small, finite catalogue* of closed-surface builders — tube, torus, band, Klein
   figure-eight, mirror box — is selected from that pattern. `kleinSurf`'s pinch is not
   lost: it becomes the `Klein` builder, reached by every variant whose gluing is a rank
   flip, instead of by one variant's name.
2. **A D ≥ 3 overture is the layout, extruded.** Above 2-D there is no faithful embedding
   to draw, and there never was — `cube5`/`hyper4`/`t6` are already stylised. The honest
   generalisation is to animate the board into the *very grid `view::layout` already
   computes* (`src/view/layout.hpp:79`): flat, then spaced along the depth axis, then
   fanned into a lattice of slices. That is derived from `DimSpec`/`ViewConfig`, needs no
   new shape, and is the same picture the game will show.
3. **The demo move is the variant's own.** Pick a signature piece and a signature
   destination by running the real `expandAtom` and the real `tracePath` — the same call
   M7.1.4 makes for the editor's move preview and M11 consumes for the camera. A torus
   variant shows a rook wrapping; a Klein variant shows it come back reversed; an atomic
   variant shows its capture. Nothing about the move is authored per scene.
4. **The arc is authored once, for all of them.** Open flat → dwell → form → show the
   move → unwind. The timings are the shared `OverturePlayer` cycle
   (`src/app/overture.hpp:65`), unchanged.

The single invariant that keeps this honest: **derived surfaces obey the same closure
arithmetic as the hand-authored ones, and the same test checks both.** `overtureSurfaceAt`
(`src/render/overture_scene.hpp:118`) exists precisely because `klein` joining its rank
edges with the file reversed was got wrong silently once. A derived surface is not allowed
to re-open that hole. **[INVARIANT]**

---

## M13.0 Preconditions and placement

Depends on M11 (the shared camera-shot planning; the derived arc reuses it) and on the
engine's `expandAtom`/`tracePath` (M4/M5, done) for the demo move — **not** on M7's editor;
when M7 lands it shares the same move-preview logic, but M13 does not wait for it. Feeds
M9: it is the reason a Workshop variant animates at all. It is **Wave 1, third in the
roadmap's release sequence** (after M11 and the M12 export half), because every variant
animating is both polish and the prerequisite for M9's user content. Were M13 absent, a
Workshop variant already falls back to the standard board (`overtureFor`,
`src/app/overture.hpp:38`), which is acceptable, not broken — so M13 buys quality, not
correctness.

---

## M13.1 The derived signature

One new value type turns a resolved variant into the few facts an overture needs:

```cpp
namespace cb::app {

/// What the geometry looks like to an overture. Derived, never authored.
struct OvertureSignature {
  SurfaceKind surface{SurfaceKind::Flat};  // Tube|Torus|Band|Klein|MirrorBox|FlatGrid
  std::uint8_t dims{2};
  bool temporal{false};       // a turn or multiverse axis exists
  bool hasForcedCapture{false};
  bool hasRadiusEffect{false};  // a rule the burst can illustrate
};

OvertureSignature overtureSignature(const VariantSpec& v) noexcept;
}
```

`SurfaceKind` is derived purely from the identification pattern: no gluing → `FlatGrid`;
one periodic axis → `Tube`; two → `Torus`; a periodic axis with a sign flip → `Band`;
a flip plus a periodic axis → `Klein`; a mirror identification → `MirrorBox`. The
mapping is a pure function with a table test over every shipped variant.

**`Overture` the enum stays** — it becomes the *hand-authored override* key, and
`overtureFor(name)` keeps returning it for the marquee variants. What changes is the
selection: a variant with a bespoke scene uses it; everything else is built from its
signature. `overtureScene` gains a sibling:

```cpp
/// The scene for a variant with no hand-authored one. Pure in `t`, like its sibling.
OvertureScene derivedOvertureScene(const VariantSpec& v, float t, const view::Theme& theme);
```

---

## M13.2 The surface catalogue (the sacred part)

Six builders, each a pure `(u, v) -> OvVec3`, generalising the `app::Overture` surface
functions of the same name. The seam-closure property is the acceptance test, and it is
stated as arithmetic per surface, not as an image:

- `Torus` — a point on the ring of rings; closure `(0, v) == (1, v)` and `(u, 0) == (u, 1)`.
- `Band` — a Möbius band; closure `(u, 0)` lands on `(1, u)` reversed in the normal.
- `Klein` — the figure-eight cross-section; `(u, 0)` lands on `(1 - u, 1)`, and the test
  asserts the file reversal is *real* rather than a shifted circle. **This is the test
  that caught the original bug; it must catch the same bug in the derived path.**
- `Tube`, `MirrorBox` — the trivial cases.
- `FlatGrid` — not a closed surface; above 2-D the "surface" is the extruded `layout`, and
  the closure property does not apply (the current code returns the flat board here).

`tests/render/test_overture_scene.cpp` already states each variant's gluing as arithmetic;
M13 extends it so that the *derived* surface for a variant equals the hand-authored one
where both exist. That differential test is what makes "derive it" safe: the shipped
scenes become the oracle for the generated ones.

---

## M13.3 The derived demo move

From the resolved variant:

- **Choose the piece.** The first non-royal mover with an atom that wraps or travels an
  extra axis; fall back to the first piece. Deterministic and documented.
- **Choose the move.** Enumerate that piece's moves from the start position, prefer one
  that crosses an identification (`PathStep::kind == Portal`) or lands on a captured
  piece — a move that *shows the variant*. Reuse `expandAtom` and `tracePath` unchanged.
- **Draw it.** Tokens along the route (an `OvToken` at each run start), a `dashed`
  `OvTrail` for a leap and a solid one for a slide, and an `OvBurst` when the move is a
  capture the signature flags (`hasRadiusEffect` → the atomic ring; a plain capture → a
  smaller burst). All existing `OvertureScene` elements — **no new output types.**

This is the same code path the editor's preview and the M11 camera use, so "what does this
piece do on this board" has one answer everywhere.

---

## M13.4 The camera arc

The overture's `OvCamera {yaw, elev, reach, persp}` (a mini-camera, not `view::OrbitCamera`)
is driven by a short `CameraShot` sequence from M11: settle to the flat board, rise to
reveal the surface, hold while the piece moves, pull back for the formed shape. Because
`reach` is a function of `t` (the header says so at `overture_scene.hpp:80`), the zoom
cannot drift out of step with the shape. The arc is authored once and shared by every
derived scene; only the surface and the move differ.

---

## M13.5 Selection and overrides

- `OverturePlayer::select(std::string_view)` (`src/app/overture.hpp:93`) becomes
  spec-aware: it takes the variant, computes the signature, and prefers a bespoke
  `Overture` if `overtureFor(name)` names one, else the derived scene. The player's
  "never cut off" rule (`app/overture.hpp:45`) is unchanged.
- The fourteen hand-authored scenes are kept and remain the reference look. A variant is
  never *worse off* for having a derived scene — the fallback discipline is the same as
  `archetypeFor`: an unknown variant animates rather than showing a blank pane.
- **Cosmetic, like every asset.** The signature is derived from the spec and the scene
  enters nothing; an overture change must not touch `VariantId` (the M7.0.2 rule).

---

## M13.6 Rule-flavoured scenes (best effort, explicitly partial)

Variants whose *point* is a rule rather than a shape — `atomic`, `mustcapture`,
`charged` — are the hardest to derive, because the drama is in the effect. M13 does not
promise to generate them. It promises only that their signature flags (`hasForcedCapture`,
`hasRadiusEffect`) can drive a burst, and that where that is not enough the bespoke scene
is used. Stated as a non-goal so the milestone cannot be judged on it.

---

## M13.7 Tests and acceptance

- **Signature table:** every shipped variant maps to the expected `SurfaceKind`; a
  variant with no gluing maps to `FlatGrid`.
- **Derived == authored:** for `cylinder`, `torus`, `mobius`, `klein`, `mirrorbox`, the
  derived surface equals the hand-authored one pointwise (differential; the shipped
  scenes are the oracle).
- **Closure arithmetic:** the derived `Klein` reversal and `Band` half-twist are asserted
  as the existing test asserts them — the same facts, on the general path.
- **Determinism:** `derivedOvertureScene(v, t, theme)` twice at the same `t` is
  byte-identical; it reads no clock (the purity rule the overture already has).
- **A Workshop-shaped variant:** a variant built only as a `variant.toml` — its name in no
  C++ table — produces a non-empty scene at `t = 0, .5, 1` and passes the `--shot` gate.
- **No regression:** every bespoke overture's `--shot` frame is unchanged.

### Acceptance facts

1. A variant introduced purely as data animates on the library screen with no scene code
   written for it.
2. The derived surface for each 2-D topology satisfies that topology's closure arithmetic,
   checked by the same test the hand-authored surfaces pass.
3. A D ≥ 3 variant's overture is the extruded `view::layout` grid, derived from
   `DimSpec`/`ViewConfig` — no per-variant geometry.
4. The demo move is a real move of a real piece, traced by `tracePath`, not a scripted
   path.
5. Every hand-authored overture is unchanged, and a variant using one is never downgraded
   to the derived scene.
6. Overture selection changes nothing in `VariantId`.

---

## M13.8 Docs and skills

- `docs/overtures.md` — the signature table, the surface catalogue and its closure
  arithmetic, and the derived arc.
- Update `docs/ARCHITECTURE.md` §10: overture surfaces as a derived, catalogue-keyed
  projection, like the board's.
- Update `AGENTS.md`: the overture section gains "a variant with no bespoke scene gets the
  derived one", and the gotcha list keeps the Klein closure note with a pointer to the
  general test.
- Skill after the third repetition: `cb-new-overture-surface`.

---

## Dependencies and ordering

1. **M13.1** the signature and `SurfaceKind` (table test) — everything keys off it.
2. **M13.2** the surface catalogue + derived==authored differential — the correctness core.
3. **M13.3** the derived demo move (reuse `expandAtom`/`tracePath`).
4. **M13.4** the camera arc (reuse M11 shots).
5. **M13.5** selection and overrides; **M13.6** the partial rule-flavour work.
6. Tests and docs land with each step (`cb-tdd-step` loop).

## Risks and non-goals

- **Procedural beauty is not bespoke beauty.** Derived scenes will be *consistent and
  pleasant*, not as striking as the tuned ones. That is the accepted trade: the marquee
  variants keep their hand-authored scenes; the tail gets something good. The milestone is
  judged on "the thousandth variant animates and is correct", not on matching the first
  fourteen.
- **The closure tests are the whole risk.** A generated surface that is subtly wrong is
  invisible in a still and wrong in motion. The differential against the shipped scenes is
  not optional.
- **Rule-drama variants are explicitly partial** (M13.6). Do not claim them.
- **Non-goals:** no scene scripting, no authoring UI for overtures, no change to the
  `OverturePlayer` cycle, no new `OvertureScene` element types, no engine change. The
  milestone lives in `render`/`app` and consumes `VariantSpec` read-only.

## Status

Planned, not started. The generic path is new, but it rests on existing, tested pieces:
the identification model (M3), `view::layout` (M4), `expandAtom`/`tracePath` (M5/M4), and
M11's camera shots. The hand-authored scenes become the oracle for the generated ones, so
the risky part — surface closure — is checked against something already correct.

### As built (2026-09-30)

`app::overtureSignature` derives a `SurfaceKind` from the identifications (never the name);
`render::derivedSurfaceAt` maps it to the same warps the authored scenes use, asserted
pointwise-equal to them; and `Ui` uses the authored scene when the name has one, else
`derivedOvertureScene`, so a variant that exists only as TOML animates with no scene code.
The derived scene opens on the variant's own start position and plays a real traced move
(M13.3) for a 2-D 8×8 board. Tests: a signature table over every shipped variant, the
differential surface, and a data-only TOML torus. Docs: `docs/overtures.md`.

**Also built:** the D ≥ 3 extruded-`layout` overture (`derivedGridOverture`), so a data-only
3-D/4-D variant animates into its own lattice; it compiles and the render/view tests pass but
has not been eyeballed on a machine with a display.

**Deferred:** rule-flavoured scenes (M13.6, a stated partial). `--shot` of a derived variant
forces the library picker to it.

## M13.9 The main-menu attract sequence: a played game that changes shape

Requested directly, for trailer production: "do the game with changing geometries [in
the main menu]... program piece moves with 'shape' morphs, start on the regular board."
Unlike M13.1-M13.8 above, this is not the New Game screen's per-variant preview (a pure
function of `(spec, t)` with no real game state) - it is a real, short game, actually
played move by move, on the **main menu**, that cuts and morphs between several
variants' own geometries while it plays. Scoped explicitly "for now": piece moves and
shape/ghost morphs only - no new camera work. The user is building the camera
choreography separately.

### A. A trailer-scoped 8x8 nested-shell variant

`cylinder`, `torus` and `klein` (`variants/*.toml`) are already 8x8 - the same file/rank
count as `standard`. `torus3d` is 4x4x4. For the sequence's "it's the same board,
gaining a dimension" beat to actually read that way, the 3-D step needs to match: a new
`variants/torus3d_8x8.toml`, file=8/rank=8/level=4 (256 cells, the same scale `hyper4`
already renders), built the same way `torus3d.toml` is - three periodic identifications,
no code - with a comment stating plainly that it is trailer-scoped, not a catalogue
variant (matching `torus3d_twist.toml`'s own precedent of a special-purpose 4x4x4
variant already in the repo).

That alone is not enough: the D >= 3 play-shape dispatch
(`src/render/overture_scene.cpp`'s `playShapeAt`, the `d.dims() == 3` branch, and
`hasPlaySurface`'s matching check) is gated by **literal variant name** -
`v.name == "torus3d"` - not by structure. A new variant under a different name gets
nothing and falls back to the ordinary flat lattice. Fix this by generalizing the gate
to a structural check - three dimensions, every axis periodic (the same boundary-kind
inspection `PlaySurface::buildStacked` already reads per axis, `src/render/
play_surface.cpp`'s `periodicU`/`periodicV`-style checks, extended to a third axis) -
rather than adding a second hardcoded name. This is the generalization the codebase's
own stated ethos already argues for (`torus3d_twist.toml`'s own comment: "the gluing
language has no dimension-specific case"), and it has a useful side effect worth noting,
not chasing: `torus3d_twist` (shipped, 4x4x4, non-orientable) currently has no play
shape for the identical name-gating reason, and would gain one for free. That is a
welcome consequence of the fix, not a second task.

**Tests.** `hasPlaySurface` true for the new variant; a `playShapeAt` position check at a
couple of coordinates; `torus3d` and `hyper4`'s own existing behaviour completely
unchanged (regression) - and `torus3d_twist` gaining a working play shape is an
acceptable, expected side effect to note in the status write-up, not a scope violation.

### B. The attract sequence: a data model, and a driver that reuses the watch machinery

**Reuse, do not reinvent.** `Session::startWatching(GameFile, dwell)` /
`advanceWatching(dt)` (`src/app/session.hpp:235-249`, impl `session.cpp:710-776`) is
already the per-frame driver for "play scripted moves over time, with animation,
interactively" - built for M12.8's "Watch a game," and already proven at that job. An
attract sequence is a **looping sequence of these**, one per geometry beat, with the
shape/ghost pose also eased per step - new orchestration layered on top, not a new
move-timing mechanism.

A plain data structure - name it for what it is, e.g. `AttractStep{ variant, moves
(a GameFile), formedTarget, ghostTarget }`, `AttractSequence = vector<AttractStep>` - and
a driver on `Shell`, `startAttract(AttractSequence)` / `advanceAttract(dt)`, built the
same shape as `startWatching`/`advanceWatching` (`src/app/shell.cpp:406-441` is the
pattern to follow, not reinvent). Per step: `startGame(step.variant)` (full variant
switch/reset, `shell.cpp:306-325`), `session_->startWatching(step.moves, watchDwell)`,
then every frame ease `settings_.geometryFormed`/`settings_.geometryGhost` toward the
step's targets (`render::advanceFormMorph` already does this for `formed`
- `src/gui/main.cpp:1390-1413` is the existing call site to read before adding a sibling
for ghost, or reusing the same helper if it is already generic enough). Advance to the
next step - wrapping back to 0, looping forever - once **both** the step's moves are
exhausted (`advanceWatching` returns false) **and** the pose has settled near its
target; never cut mid-ramp.

**Two caveats to build around, not paper over** - both already documented in this
plan's M18.6 work, not new discoveries, but easy to trip over here specifically:

1. **`hyper4` has no continuous roll - it is a snap**, per M18.6's own status note
   ("honestly a snap - there is no continuous roll to expose"). Its step's
   `formedTarget` reaching 1 will pop, not ease, no matter what this task does. Do not
   spend effort trying to smooth it - that is out of scope here (and already out of
   scope in M18.6). A hard pop reading badly is a pacing/camera problem for later.
2. **`geometryFormed = 0` is a rank-mirrored flat board**, not the ordinary flat
   lattice (M18.6, documented, deferred - the same "180 mirror" item this plan's own
   Klein/morph revision flagged as still unaddressed). The *first* step's flat ->
   shape reveal - the "disguised as a regular board" opening the user asked for - may
   therefore show visibly mirrored ranks on its very first frame, not a clean echo of
   `standard`. **Build it, then actually look at a capture of it.** If the mirror reads
   as wrong, the documented fallback is: open on the literal `standard` variant for the
   sequence's first second or two, then hard-cut to the cylinder step already mid-reveal
   (`formed > 0`) - sidestepping the open bug rather than fixing it here. State plainly
   in the status note which of the two shipped, and why.

**Variant-switch side effects.** `Shell::startGame`/`startWatching` do not themselves
push renderer options or re-frame the camera - the existing "watch a different bundled
game" request handler (`src/gui/main.cpp:1460-1477`) does that explicitly, at the call
site, after `Shell::startWatching` returns, including whatever it already forces for
`cameraMode`/`geometryAlign` on a glued variant. `advanceAttract`'s callers need the
identical treatment on **every** cut, not just the first - read that handler as the
pattern and inherit it as-is. Camera behaviour is explicitly out of scope for this task;
whatever "Watch a game" already does today is what ships here too, unchanged.

### C. Wire it into the main menu

Today `Screen::MainMenu` draws only the ambient `Deco::Manifold` field (`decoForScreen`,
`src/render/deco.cpp:572-600`), and `Shell::showsBoard()` (`shell.cpp:165-180`) returns
`false` for it - no board is ever drawn on the main menu. While an attract sequence is
running: `showsBoard()` should return `true` for `MainMenu` (so the existing board path -
the one `Screen::Game` already uses, no new renderer code - picks it up), and
`decoForScreen` should stop drawing `Deco::Manifold` for `MainMenu` in this state (the
board replaces the ambient field, not alongside it). Drive it from the same per-frame
spot `advanceWatching` already runs from (`src/gui/main.cpp:1425`): call
`shell->advanceAttract(dt)` whenever `screen() == Screen::MainMenu` and a sequence is
running. Start the sequence when the app opens on the main menu (always-on is simpler
than an idle-timeout trigger and matches "implement it in the main menu" directly -
default to it unless it conflicts with something already there).

**Do not touch input handling.** Every existing `MainMenu` button (New Game, Watch,
Settings, Quit) must keep working exactly as today, laid over the now-playing board the
same way the pause menu already composites its UI over a live board (`Screen::Paused`) -
reuse that layering, do not invent a new one.

### D. The sequence content

`cylinder` (8x8, exists) -> `torus` (8x8, exists) -> `klein` (8x8, exists, ghost target
~0.35) -> `torus3d_8x8` (new, part A, ghost target ~0.35) -> `hyper4` (exists, 4x4x4x4,
`formed` snaps per the caveat above) -> loop back to `cylinder`. Only the **first** step
(`cylinder`) ramps `formed` 0 -> 1, the reveal the user described; every later step
starts already-formed and simply cuts - repeating the flat-reveal trick at every step
would read as slow and repetitive rather than escalating, which is the pacing judgement
call for this part. Each step carries 2-4 ply of **real, legal** opening moves - author
them for real and let the engine's own legality check prove it: a `GameFile` that fails
to load or replay (`io/game_file.hpp`'s `applyGameFile`) is itself a build-time test
failure here, not something to hand-wave past. Ghost ramps (`klein`, `torus3d_8x8`) ease
the same way `formed` does, over the step's own duration, settling before that step's
"moves exhausted and pose settled" advance condition can fire.

### Tests

A pure logic test (no GPU, same shape as `tests/unit/app/test_shell.cpp`/`test_watch.cpp`)
stepping an `AttractSequence` through `advanceAttract(dt)` ticks: each step's variant
loads, its moves replay legally, and after enough ticks the sequence wraps back to step
0 and keeps going - proving the loop, not just one pass. A `showsBoard()`/
`decoForScreen()` test confirming both flip correctly while attract is active on
`MainMenu`, and are unchanged on every other screen (regression). Part A's own tests as
above. Note plainly, the way M17.12 part B's status note did: a live, looping,
many-minutes sequence cannot be exercised end-to-end headlessly, but every piece of its
*state* (which variant is loaded, the current `formed`/`ghost` targets, whether a step
has settled) is directly testable without a display - capture a `--shot` at a couple of
hand-picked points in the sequence (mid-cylinder-reveal, klein-with-ghost) for a visual
spot-check, and say plainly that is a spot-check, not full coverage.

### Acceptance

Launching the app and sitting on the main menu shows the sequence playing automatically:
a real short game, the board visibly changing shape between beats, ghost visibly
engaging on the klein and `torus3d_8x8` beats, looping back to the start without a
restart. Every existing main-menu button still works, laid over the playing board. No
other screen's behaviour changes - the New Game screen's own M13 overture system, Game,
Paused and Watch are all exactly as before.

### Risks and non-goals

Camera is explicitly out of scope ("I'll figure out camera moves later") - ship whatever
`Shell::startWatching`'s existing glued-variant defaults already do; do not add new
framing, follow, or orbit behaviour. Do not touch the New Game screen's own overture
system (M13.1-M13.8 above) - a separate, already-working code path. `torus3d_8x8.toml`
is explicitly trailer-scoped, per the user's own words ("just for this trailer") - do
not add it to any curated/"start here" list or other player-facing surface beyond what
this task needs. The `formed = 0` mirror and the `hyper4` snap are known, pre-existing,
documented limits (M18.6) that this task works around, not fixes.

### Status: M13.9 built (2026-10-05, opencode)

**Built, in the four lettered parts, each green before the next.**

**A - the trailer variant and the structural gate.** `variants/torus3d_8x8.toml` is an
8x8x4 (256 cells) all-periodic torus, `torus3d.toml`'s construction at the
sequence's 8x8 scale, commented as trailer-scoped. The D >= 3 play-shape dispatch is no
longer keyed on `v.name == "torus3d"`: a new `allAxesPeriodic` reads
`boundaryKind(axis, Side::Max) == Periodic` for all three axes, and both `playShapeAt`'s
3-D branch and (through `playShapePosition`) `hasPlaySurface` now follow structure. Because
the old formula divided the file/rank coordinates by the hardcoded `kE = 4`, each is now
normalised by the board's own extent: `torus3d` (extent 4) is bit-identical, and the
8-wide board wraps the tube once instead of ~1.9 times and folding its tiles onto each
other. `torus3d_twist` gains a working play shape for free, as the spec predicted.

**B - the sequence and its driver.** `AttractStep{variant, moves, formedTarget,
ghostTarget, formedFrom}`, `AttractSequence`, `defaultAttractSequence()`, and on `Shell`
`startAttract`/`stopAttract`/`advanceAttract`/`attracting`/`attractStep`. Each beat is a
full `startGame`/`Session::startWatching` (the M12.8 watch machinery, reused), and
`advanceAttract` eases `geometryFormed` and `geometryGhost` toward the beat's targets and
cuts only once the game has run out *and* the pose has settled. The easing helper
`advanceFormMorph`/`kFormMorphRate` moved down to a new header-only `src/view/morph.hpp`,
re-exported from `render/play_surface.hpp`, so the app layer can share the one definition
rather than duplicate it (app L95 may not include render L100).

**C - the main menu.** `Shell::showsBoard()` returns true for `MainMenu` only while
attracting; `decoForScreen(screen, boardBehind)` returns `None` for the menu when a board
is behind it (default unchanged, every other screen unchanged). The menu build now uses
`pauseFrame`'s layering - board left behind a thin scrim, menu right - instead of
`drawShellFrame`, whose opaque ground would paint over the board; `Screen::Paused` and
`Screen::Settings`-over-a-game already worked exactly this way. The interactive loop starts
the sequence on `MainMenu`, drives `advanceAttract` every frame, stops it the moment the
player navigates away, and on every cut pushes `optionsFor` and re-frames the camera with
the same `geometryView`/`cameraMode="route"`/`followStrength` the "watch a different game"
handler already forces. Input handling is untouched. One extra: `showsCheckWarning()` is
suppressed on `MainMenu`, because the attract board is ambient footage and a red check
vignette there reads as an error, not a game warning.

**D - content.** `standard` (opener) -> `cylinder` -> `torus` -> `klein` (ghost 0.35) ->
`torus3d_8x8` (ghost 0.35) -> `hyper4`, looping. Every beat's 2-4 ply are real, legal
opening moves and are proven so by an `applyGameFile` test and by the runner replaying
them; the Ruy line, a torus seam capture, a Klein rook capture and a 3-D rook/knight
exchange are the lines. `hyper4`'s `formed` snap is inherited and untouched, per the
caveat.

**The `formed = 0` mirror: the documented fallback was shipped, not the opening.** A
capture of the cylinder beat at `formed = 0` (before the fallback) shows the flat board
with black nearest the camera and white far off - the reverse of an ordinary board, the
M18.6 rank-mirror bug. Rather than ship that, the sequence opens on the literal `standard`
variant for two plies, then hard-cuts into the cylinder beat already at
`geometryFormed = 0.30`, rolling the last 0.70 in motion. The standard opener is a separate
beat (`formedFrom = 1`), so the sequence is six steps, not five, and the reveal is a cut
plus a short roll rather than a full 0 -> 1 ramp - the fallback the spec names.

**Tests.** `tools/test.sh --build render` (138 cases) and `--build app` (73) green after
Part C, plus `[attract]` (4 cases / 42 assertions): every beat's file replays legally,
the sequence visits all beats in order and wraps to step 0 while still running, `stopAttract`
restores the opaque board, and `showsBoard` flips only on `MainMenu`. `[variants]` green
with the new variant, `[arch]` green, `test_deco` pins the `decoForScreen` flip and its
non-leakage, `test_play_surface` pins the 8x8 play shape, the extent-normalised one-lap
period and the unchanged `torus3d`/`hyper4` behaviour. `ctest -R gui-(menu|settings|pause|
board|watch|newgame)` 8/8 validation-clean, and `tools/precommit.sh` green (build, arch,
unit, property). `tools/precommit.sh`'s format step only inspects staged files, so
`clang-format --dry-run --Werror` was also run by hand over every touched source and is
clean.

**What could and could not be visually checked.** This is a spot-check, not end-to-end
coverage: a live, many-minute looping sequence cannot be exercised headlessly (the dummy
SDL video driver cannot create the Vulkan window the interactive loop needs, so the real
loop was not run). The reviewable capture path (`--shot --screen menu --t S`) was extended
to run the sequence for `S` seconds at a fixed step, and captures were taken and inspected:
the standard opener (white nearest, correctly oriented), the cylinder mid-roll, the
ghosted Klein and 3-D torus (pieces visible through the shell), and `hyper4` with its
tesseract scaffold. A single captured frame does not ease the camera the way the live loop
does, so its framing is approximate and occasionally crops the shape; the interactive
framing eases home over the following frames. The `hyper4` beat was seen only as a
fully-formed cut.

**Known, unchased.** `torus3d_8x8.toml` is auto-scanned into the New Game library like any
other file (the same as `torus3d_twist`); it is in no curated list, per the spec, but it is
player-visible because the picker globs the directory. `startAttractStep` goes through
`startGame`, so each beat persists settings and rewrites `lastVariant` - harmless (the
library always opens on `standard`) but worth a lighter path if the sequence ever gets a
settings-menu interaction. Suppressing the check vignette on the menu is a small behaviour
change the spec did not name; the alternative (a red flash mid-trailer) read clearly worse.

### Revision: a button instead of an always-on takeover, a static camera, and one
### continuous game across every shape (2026-10-05, feedback)

Three corrections, after actually seeing what shipped:

1. **This should never have replaced the quintic as the menu's default.** "Implement it
   in the main menu" meant *an entry point from* the menu, not *the menu's own state*.
   The quintic manifold (`Deco::Manifold`) stays what a player sees on an ordinary visit
   to `Screen::MainMenu`; the sequence only plays when asked for, the same way "Watch a
   game" only plays a bundled game when the player picks one.
2. **The camera must not follow the pieces.** The previous build's `applyAttractLook`
   forced `cameraMode = "route"`/`followStrength = 1.0` - inherited from "watch a
   different game," which is exactly the move-chasing behaviour the camera-choreography
   work (being built separately) needs to own instead. Re-framing *at a beat's own cut*,
   so the newly-revealed shape is actually in view, is still wanted and should stay; what
   must go is any per-frame following of a piece mid-slide within a beat.
3. **One continuous game.** Every beat currently starts from its own variant's canonical
   opening and plays its own short, independent scripted line - so each "morph" is really
   a fresh game that happens to look similar, not the same game continuing. The whole
   six-beat sequence should be one running game: the position a beat ends on becomes the
   position the next beat starts from, carried forward rather than reset. Chosen
   explicitly over the alternative (continuity only within the three same-dimension 2-D
   beats, a fresh start at each dimension change): **the position is embedded on one
   slice** when a beat steps up in dimension - the running position's pieces land at the
   same file/rank/level coordinates in the bigger lattice, with every newly-introduced
   axis (level, then aeon) fixed at a single value for every carried piece, so the rest of
   that new axis starts empty. The game is still real and still provably legal at every
   step - nothing about this fakes a position or skips the engine's own legality check.

**Part E (small, do first): the button and the static camera.**

Revert the always-on trigger in `src/gui/main.cpp`'s per-frame `MainMenu` block (the
`if (!shell->attracting()) { shell->startAttract(...); ... }` that starts a sequence the
instant the app sits on the menu) - starting the sequence belongs behind a new menu
action, not the per-frame loop. Add a sibling row to "Watch a game" in
`Ui::buildMainMenu` (`src/render/ui_menus.cpp:138-197`; the existing row is built with
`menuEntry("Watch a game", 2, t, display, width, true, scale_)` at line ~169, routing to
`shell.go(app::Screen::Watch)` - `menuEntry`'s signature is
`src/render/ui_widgets.hpp:124-125`). A new row immediately after it (shift every later
row's printed index by one) that calls `shell->startAttract(defaultAttractSequence())`
directly - this sequence has no picker step the way bundled "Watch" games do, so it does
not need its own screen, just a button that starts it in place. Reuse the existing
`decoForScreen`/`showsBoard` plumbing exactly as built (it already does the right thing:
the board replaces the manifold *while a sequence is actually running*, which after this
fix only happens after the button is pressed).

Give it an exit, matching the house precedent rather than inventing a new one: while
`shell.watching()` on `Screen::Watch`, `src/render/ui.cpp:1468-1479` shows a single
`"STOP WATCHING"` button plus an `Esc leaves the game` caption, wired to
`Shell::stopWatching()` (`shell.cpp:437-440`) and to `Esc` at `src/gui/main.cpp:1332`.
Do the same for attract: a `"STOP"`-style control (visible only while
`shell->attracting()`) calling `shell->stopAttract()`, and the equivalent `Esc` wiring.

For the camera: in `applyAttractLook` (`src/gui/main.cpp`, near the function added for
M13.9), stop forcing `cameraMode = "route"`/`followStrength = 1.0f` - set
`cameraMode = "off"` and `followStrength` to `0.0f` instead (`Session::setCameraMode`/
`setFollowStrength`, `src/app/session.hpp:108-113`; `"off"` means no per-frame following
at all - confirmed at `src/gui/main.cpp:363`, `if (st.cameraMode == "off" || ... ) { ...
skip the follow update ... }`). Keep whatever re-frames the camera *on a cut*
(`frameBoard`, already called every time `advanceAttract`/`startAttractStep` reports a
new beat) - that is not "following," it is making sure the new shape is visible at all,
and removing it would leave the camera pointed at nothing after every cut.

Build and verify this part alone first - it is small, low-risk, and does not depend on
part F at all.

**Part F (the continuous game): position embedding across variant switches.**

A new helper - a free function, pure, independently testable, living wherever it reads
most naturally alongside `Position`/`DimSpec` (the `app` layer already sees both; keep it
out of `render`) - call it something like
`Position embedOnto(const Position& from, const VariantSpec& fromVariant, const
VariantSpec& toVariant)`. For each occupied cell in `from` (`Position::isEmpty`/`at`,
`src/position/position.hpp:28-31` - there is no ready-made "for each occupied cell"
iterator, the idiom used throughout the renderer is a plain loop over `cellCount()`
checking `isEmpty`, e.g. `src/render/board_renderer.cpp:688`): decode its `CellId` to a
`Coord` on `fromVariant.dims` (`DimSpec::toCoord`, `src/space/dim_spec.hpp:75`), build a
`Coord` on `toVariant.dims` by **matching axes by name** (`DimSpec::axisIndex`,
`dim_spec.hpp:57` - more robust than assuming position, though every variant in this
sequence happens to declare file/rank/[level/[aeon]] in that same order) rather than
blindly copying by index, carrying every axis `from` already has and setting every axis
only `to` has to a fixed value (0 - "level 0", "aeon 0"), then re-encode with
`DimSpec::toCell` (`dim_spec.hpp:67`) and `place` the same piece type/colour into a fresh
`Position(toVariant)` (`position.hpp:22`, `Position::place`, `position.hpp:49`). Side to
move carries forward too. This mapping is injective - and therefore safe, no two source
cells ever colliding onto one target cell - exactly when every shared axis's extent in
`toVariant` is >= its extent in `fromVariant`; state this precondition in a comment where
the helper is defined, since it is what makes "embed on one slice" a safe operation here
rather than a general-purpose one.

Rework `Shell::startAttractStep` (`src/app/shell.cpp`, ~line 480 on): capture the
*current* position from the session **before** calling `startGame` (which replaces
`session_` wholesale - confirm the exact live-position accessor on `Game`,
`src/game/game.hpp`, alongside the already-known `startPosition()`/`setStartPosition()`;
it is not necessarily named `currentPosition()`, check before using it). After
`startGame(step.variant)` constructs the new beat's fresh session, build the new beat's
`GameFile` the same way as today (`toGameFile(session_->variant(), session_->game())`,
`io/game_file.hpp:31`) but then **overwrite** `file.startFen` with
`toFen(embedOnto(previousPosition, previousVariant, step variant's VariantSpec))`
(`toFen`/`fromFen`, `io/fen.hpp:18,21`) rather than leaving `toGameFile`'s own
canonical-start default - that default is exactly what currently makes every beat reset.
The very first beat (`standard`, nothing to carry forward into it) keeps today's
behaviour unchanged - there is no previous position for the sequence's own opening.
`GameFile::startFen` already is a free-form string (`io/game_file.hpp:26`) - nothing
downstream (`Session::startWatching` → `GameRunner::reset` →
`Session::loadFen`, `session.cpp:664-672,710-739`, `game_runner.cpp:13-21`) assumes it
matches the variant's own declared opening, so this is a drop-in substitution, not new
plumbing.

**The wrinkle this surfaces, not to be quietly worked around:** `hyper4` is a fixed
4x4x4x4 lattice - *smaller* in file and rank than the 8x8 the rest of the sequence now
uses, `torus3d_8x8` included. The injective-mapping precondition above fails exactly at
the last beat: an embedded file/rank coordinate from an 8-wide board has nowhere
legal to land on a 4-wide one. Mirror the fix this same revision already made for
`torus3d`: a similarly trailer-scoped `variants/hyper4_8x8.toml`, file=8/rank=8/level=4
(matching `torus3d_8x8` exactly, so that beat's embedding is a trivial identity on its
first three axes) and aeon=2 as the one genuinely new axis (512 cells total - noticeably
more than the 256 the hyper4 scaffold/rotation work was verified against; actually check
this renders and performs acceptably before treating 512 as fine, rather than assuming
it). If 512 proves to be a real problem, aeon=1 (256 cells, unchanged scale) is the
fallback, but say so plainly rather than silently shrinking it - an aeon axis of size 1
has nowhere for "the fourth dimension" to actually go, which is a real loss for this
beat's own point.

Every beat's scripted moves (`AttractStep::moves`) need to be real, legal continuations
from whatever that beat's *embedded* starting position actually is - which, past the
first beat, is no longer any variant's own canonical opening, so it cannot be guessed or
reused from the previous build. Compute each beat's true starting FEN for real (run the
sequence through the engine up to that point - a throwaway test or scratch tool is fine
for authoring, same as how every other move list in this plan has been required to be
engine-verified, never hand-guessed) before choosing that beat's next few moves.

**Tests.** A unit test for `embedOnto` alone: a small source position, embedded into a
same-dimension target (identity - every piece lands exactly where it started) and into a
higher-dimension target (every piece's new axis is 0, the rest of that axis is empty,
existing axes unchanged). The existing `[attract]` "every attract beat is a real, legal
game for its variant" test (`tests/unit/app/test_attract.cpp`) no longer makes sense
beat-by-beat against each variant's own canonical start - rewrite it to walk the whole
sequence once, computing each beat's embedded start from the previous beat's end exactly
as `startAttractStep` now does, and proving the whole chain legal end to end. A property
check across the full sequence: total piece count never increases from one beat to the
next (only captures remove pieces; embedding must never duplicate or drop one without a
capture to explain it) - a cheap, global guard against exactly the kind of bug a
coordinate-mapping mistake would produce.

**Acceptance.** Watching the sequence start to end: the same pieces, accumulating the
same move history, carry through every shape change - no beat's board reverts to a fresh
opening array. The ordinary main menu is the quintic field until the new button is
pressed; pressing it starts the sequence; a visible control plus Esc stops it and returns
to the ordinary menu. The camera never chases a sliding piece mid-beat; it only recentres
at a cut.

**Risks and non-goals.** Camera choreography beyond "stop auto-following, keep the
per-cut re-frame" is explicitly not this task - that is being built separately. Keep
building and verifying part E before part F; they are independent and E is far smaller.
`hyper4_8x8.toml` is exactly as trailer-scoped as `torus3d_8x8.toml` - same disclaiming
comment, not added to any curated list, not a catalogue variant.

### Status: M13.9 revision - part E built; part F blocked on topology legality (2026-10-05, opencode)

**Part E (button + static camera) is built and green.** It is the small diff the spec
described.

- The per-frame auto-start on `Screen::MainMenu` is gone. The sequence now starts only
  from a new main-menu row, **"Watch trailer"** (ordinal 03, immediately after "Watch a
  game"). The row raises a new `render::UiRequest::startAttract`, handled in `main.cpp`
  exactly like the bundled-watch request (`applyAttractLook` -> options -> `frameBoard` ->
  `wasSurface`), so the camera/renderer plumbing lives where it already did. Every later
  row (Continue 4, Multiplayer 5, Tutorial 6, Editor 7, Settings 8, Quit 9) is renumbered.
- The exit matches the watched-game precedent: while `shell.attracting()`, a bottom-left
  **STOP TRAILER** overlay plus the caption "Esc leaves the trailer" is drawn (only then -
  the ordinary menu has neither), wired to `Shell::stopAttract()`. Esc checks `attracting()`
  **first**: a running attract also sets `watching()`, so without that check Esc would stop
  the inner watch and leave the sequence running.
- `applyAttractLook` now sets `cameraMode = "off"` / `followStrength = 0.0f` instead of the
  inherited `"route"`/`1.0f`. The per-cut `frameBoard` is kept, exactly as specified.
- One judgement call the verification forced: the `--screen menu --t S` review path was
  gated on an explicit `--t` (new `overtureTGiven`), so a plain `--shot --screen menu` is
  now the ordinary quintic menu rather than the trailer at its default 0.78 s. `--t 0`
  still starts the trailer from the top. Without this the acceptance's "the ordinary main
  menu is the quintic field" could not be shown.

Tests after Part E: `tools/test.sh --build render` 138 cases, `--build app` 73 (before
part F's additions), `[attract]` 4, `[variants]` 5, `[arch]` 9, `io game rules variant`
43, and `ctest -R gui-(menu|pause)` 2/2 all green. Capture review: `--shot --screen menu`
shows the quintic field with rows 01-09 and no stop control; `--t 0` / `2.0` / `4.0` show
the board behind the menu with the STOP TRAILER control and a static (not piece-chasing)
camera.

**Part F (the continuous game) was attempted and stopped partway.** What is built and
left as scaffolding:

- `app::embedOnto(from, fromVariant, toVariant)` (`src/app/embed.{hpp,cpp}`): a pure
  helper that carries every piece across a variant switch, **matching axes by name** and
  fixing every axis only the target declares at 0. Unit-tested (`tests/unit/app/test_embed.cpp`):
  identity across two same-shape variants, and new-axis-is-0 (with the rest of the new
  axis empty) for the higher-dimensional case.
- `variants/hyper4_8x8.toml`: 8x8x4x2, aeon=2 (512 cells), trailer-scoped. It loads,
  has 86 legal opening moves, and renders headlessly in 0.36 s, so the size concern the
  spec raised is not a problem. It declares the full standard six piece kinds so the
  embedding is lossless.

The reworked `startAttractStep` (capture the live position before `startGame`, overwrite
`GameFile::startFen` with the embedded FEN) was written and proven mechanically in a
throwaway generator, then reverted. It cannot be activated: **the spec's central
assumption - "still real and still provably legal at every step" - is false once the
topology changes.**

- **Piece tables are per-variant, not global.** `PieceTypeId` is declaration order
  (`src/io/variant_toml.cpp`), so a raw id copy is wrong across variants. Matching by name
  fixes that, but `klein`/`mobius`/`torus3d_8x8` declare **no pawn** and `hyper4` declares
  **no bishop or queen**. Authoring `hyper4_8x8` with all six makes the last embedding
  lossless, but the pawns still have nowhere to go at the Klein cut (the spec's "embedding
  must never drop one" cannot hold). Making `klein` declare pawns would change a catalogue
  variant's identity and its documented "no forward direction" design.
- **Topology changes reject the running position outright.** Setting the ordinary standard
  army on `torus` gives **0 legal moves** (`chessbox "load torus" "fen
  rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1" moves` -> 0; `torus`'s own,
  designed opening has 54). The pawnless army on `klein` also has 0, and `klein` cannot
  even parse a pawn FEN. A torus with the black queen removed still has only 1 legal move.
  This is the rank-wrap contact the variants' own openings are authored to avoid (the
  AGENTS gotcha "the usual one starts in check on a torus"), and it is intrinsic: carrying
  the game onto the glued 2-D boards produces a dead position, not a legal continuation.
- **The tesseract is keyed on the name `hyper4`** (`render/overture_scene.cpp`,
  `playShapeAt`, M17.12). `hyper4_8x8` therefore fails `render::hasPlaySurface`, so as a
  play shape it falls back to the ordinary 4-D lattice (confirmed by capture). Restoring
  the scaffold would mean touching M17.12, which this task excludes, and the branch's
  `axis - 2` centring is the 4-wide case and would need extent-normalising anyway.

**Why stopped, not routed around.** Full continuity with the shipped catalogue needs one
of three things, each larger than this task: (a) long cooperative lines that trade all
sixteen pawns away before the Klein cut *and* clear every wrap check before the torus cut
- a milestone-sized authoring job, not the "next few moves" the spec describes; (b) a
fallback to the target's canonical start whenever the embedded position is dead, which
directly contradicts the acceptance ("no beat's board reverts to a fresh opening array")
and would show exactly a reset at the torus and Klein cuts; or (c) editing the catalogue
variants' piece sets / touching M17.12. Per the task, this was stopped partway with the
reason recorded rather than shipping a sequence that contradicts its own acceptance.

**State left.** `embedOnto` and its tests and `variants/hyper4_8x8.toml` are in place,
building and green; `embedOnto` has no production caller yet, deliberately - the driver
that would use it is the part that cannot run. `Shell::startAttractStep` and
`defaultAttractSequence` are untouched, so the trailer behaves exactly as M13.9 shipped,
plus Part E's fixes. `tools/test.sh --build render` (138) and `--build app` (75, the two
new embed cases included) are green; `[attract]` is 6; `[variants]`, `[arch]`,
`io game rules variant` and `gui-(menu|pause)` are green. What could **not** be visually
checked: any piece carry across a cut, because the carry is not active - the sequence is
still the per-beat canonical-start one, and the captures above show that, unchanged. Like
`torus3d_8x8`, `hyper4_8x8` is auto-scanned into the New Game library (no curated list),
so it is player-visible even though nothing uses it yet.
