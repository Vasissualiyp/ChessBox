# Shell motion, and drawing the shape of a multiverse

**Status:** planned, not built. Written before the work so the decisions are arguable
before they are expensive.

Two unrelated complaints, both about the same failing: the game *computes* the right
thing and then draws something that does not say so. The shell knows it has moved a level
deeper and shows almost none of it; the multiverse knows one timeline was born from
another and draws them as unrelated rows.

---

## Part A — the shell actually moves

### What is wrong now

Pressing a menu row runs a transition, but only two things take part in it:

- the drifting polygon field, which is dollied toward the viewer (`DepthField::push`);
- the incoming menu pane, which is scaled and faded (`widgets::beginPane`, `enter_`).

Everything else cuts. The decorative object - the manifold, the 4-cube - is redrawn at
full size on the new screen with no motion at all, and the outgoing screen simply
vanishes. So the one element that reads as depth is the one element that is *not* the
subject, and the effect lands as "some specks drifted" rather than "the camera moved".

Three further faults, all cheap to fix and all making the first worse:

- there are 30 bodies in the field, of which a quarter are pieces, so at any moment only
  a handful are near enough to read as moving;
- the manifold is drawn at 0.40 of the pane's short side and overflows it;
- the ground is very close to white, which is tiring to look at for a long game.

### A1. One push, shared by everything

Introduce a single transition clock on `Ui` and derive every moving thing from it, rather
than each element owning its own idea of the motion.

```
struct ShellMove {
  float t{1};        // 0 at the start of a change, 1 when it has settled
  bool deeper{true}; // which way the camera is going
  Deco leaving{Deco::None};
};
```

From `t`, with one easing function:

| element | going deeper | coming back |
|---|---|---|
| incoming decoration | scale 0.55 → 1, alpha 0 → 1 | scale 1.7 → 1, alpha 0 → 1 |
| outgoing decoration | scale 1 → 1.8, alpha 1 → 0 | scale 1 → 0.5, alpha 1 → 0 |
| incoming menu pane | as now (0.66 → 1), alpha 0 → 1 | same |
| polygon field | impulse +3.1 | impulse −2.6 |

The outgoing decoration is drawn as a **ghost**: `drawDeco` is a pure function of a rect,
a theme and a clock, so the screen that is leaving can be drawn without running its build
function. That matters - a build function mutates shell state and can trigger navigation,
and drawing a screen that is on its way out must not be able to navigate anywhere.

The outgoing *menu* is deliberately not drawn. Re-running its build function to get it
would reintroduce exactly that hazard for a fraction of the effect.

**Files:** `src/render/ui.hpp` (the clock), `src/render/ui.cpp` (`tick`, `build`,
`drawShellFrame`), `src/render/deco.hpp/.cpp` (a `zoom` and an `alpha` parameter
threaded to every colour and to the camera scale).

**Test:** `tests/render/test_deco.cpp` - a decoration drawn at alpha 0 emits no vertices,
and one drawn at twice the zoom covers a proportionally larger area of the draw list.
Both are assertions about an `ImDrawList`, so no GPU is involved.

### A2. A field worth noticing

- 30 bodies → 84, with the polygon-to-piece ratio kept at roughly 3:1.
- Spread the depths so the near plane is never empty.
- Sizes scaled down a little, since there are now more of them.

**Test:** the existing determinism requirement still holds - the field is seeded from a
constant, and two `DepthField`s advanced by the same steps must produce the same frame.
That is what lets a headless capture be compared at all, so it gets an explicit test.

### A3. The manifold at half size

`cam.scale` 0.40 → 0.20 of the pane's short side. It is a decoration beside a menu, not
the subject of the screen.

### A4. A ground that is not white

`Theme::manifold().ink` moves from `#EBEFF6` to a slightly deeper, slightly warmer
neutral, and the plate colour follows it down so the panels stay distinguishable from the
page. The contrast test in `tests/unit/view/test_theme.cpp` already guards the thing that
could break here: body text must stay above 7:1 on the ground it is drawn on.

---

## Part B — the multiverse draws its own shape

### B1. The rails, read properly

The arrow under each timeline is currently a 0.10-wide bar with a *3-D knight's wedge* on
the end, dimmed to near the rule colour. It reads as a scratch, not an arrow.

- body half-width 0.10 → 0.26;
- colour: the accent at full strength for the timeline the game began on, and a clearly
  visible tint of it for branches - not a blend toward the rule colour;
- the head becomes a flat triangle lying in the board's plane: a new
  `Archetype::Arrow`, a thin two-sided triangular plate. Re-using the knight's wedge was
  expedient and looks like what it is;
- the rail runs a board's width past the last board, so it reads as a direction rather
  than as an underline.

**Files:** `src/render/piece_mesh.hpp/.cpp` (the archetype),
`src/render/board_renderer.cpp` (the rail block).

**Test:** extend the existing rail test - a temporal variant emits at least one `Arrow`
instance and a non-temporal one emits none. Pinning the archetype rather than counting
wedges also stops the test passing for the wrong reason when a variant happens to have
knights.

### B2. Connecting a branch to where it came from

Today a new timeline is a row of boards with no visible relationship to the game it came
out of. The engine knows the relationship and throws it away.

**Model.** `temporal::Timeline` gains the board it was born from:

```
LineId parentLine;   // the timeline the branching move arrived on
Turn   parentTurn;   // the board on it that the move landed on
```

`TimelineModel::branch()` takes that board; `Multiverse::apply` already has it in hand at
the moment it decides to branch, and currently discards it.

**View.** `Session` exposes the links in *lattice* terms, because the renderer works in
grid coordinates and must not learn what a timeline is:

```
struct TimelineLink { std::int16_t fromLine, toLine, atTurn; };
std::vector<TimelineLink> timelineLinks() const;
```

**Renderer.** For each link, draw a connector in the same colour as the child's rail,
running perpendicular to time from the parent row to the child row, positioned half a
board past the branch turn - the gap between the board that was landed on and the board
that followed it. For the reported case, a move onto `(5, 0)` that creates a timeline at
`(6, 4)` draws a bar at turn 5.5 spanning line 0 to line 4, joining the two rails.

**Test:** `tests/unit/temporal/test_branch_links.cpp` - branching records the parent
board, and a replay that creates two timelines reports two links with the right endpoints.
This is engine state, so it is tested without a renderer.

### B3. What this does *not* fix

Timeline numbering has never been checked against the reference game (`docs/plan/
M6-temporal.md` M6.0, still outstanding). Drawing the links makes a wrong convention more
visible, which is an argument for drawing them, but it does not make the convention right.

---

## Order

1. Part B1 - self-contained, and the arrow is wrong every time anyone opens the 5D board.
2. Part A3, A4, A2 - three small changes, each independently visible.
3. Part A1 - the largest, and the one whose value depends on A2-A4 being in place.
4. Part B2 - needs a change in the temporal model, so it goes last and gets its own tests.

Each step is committed separately with its tests green; `ctest --preset dev` and
`tools/precommit.sh` between steps, not at the end.
