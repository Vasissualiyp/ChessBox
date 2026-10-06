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
