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
