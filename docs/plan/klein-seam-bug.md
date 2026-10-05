# The Klein seam bug — findings so far, unresolved

Parked here on request (2026-10-06) rather than chased further blind. Pick this back up
with a specific repro from the live session, not another round of offline investigation -
see "What's needed to make progress" at the end.

## The report

From actual play, in the player's own words, twice:

1. "if we were to have an 1234... edge connecting to 8765... edge, then 1 would connect
   to 7.5, 2 to 6.5, etc. i.e. the centre of the square is connected to a 'line' between
   the squares" - a precise, half-cell-offset description of the rank seam's cell-to-cell
   correspondence on `klein`, with a nonzero `kleinShift` ("the stride") set.
2. A follow-up: "the most recent implementation made the bug worse: now the separation is
   not 0.5 square, but something more like 3.5 square" - reported after the slide-drift
   correction (`c37188b`, "fix the Klein seam drifting apart while sliding with a shift
   set") landed.
3. "I believe there also might be camera bugs, too" - unelaborated, not yet investigated
   at all.

## What's been checked, and ruled out

Three separate investigation passes, by two different methods (opencode once, Claude
twice), all converging on the same conclusion: **the rendered geometry's cell-to-cell
correspondence at the rank seam cannot be made to show the reported offset**, at any
`kleinShift`/slide combination tried. Specifically:

- **`docs/plan/M18-production-polish.md`, item 4's original status note** (opencode,
  2026-10-06): checked `SeamMap`'s seam assignment, `surfaceRail`'s rendered tint, and the
  figure-eight's own geometric crossing in `u`-space, all at the **shipped default**
  (`kleinShift = 0`). All three agreed to ~0.09 cell. **This never actually tested the
  scenario the report describes** - the report was always about nonzero `kleinShift`,
  which this pass did not try. Found a different, real, unlanded bug along the way:
  `PlaySurface::nearestBoundary` (the M17.19 glide waypoint) lands a piece's glide
  waypoint on the source seat's own centre instead of the shared edge - see
  "A real bug, found but not landed" below.
- **`docs/plan/M18-production-polish.md`'s own correction section** (Claude, 2026-10-06):
  re-investigated specifically at nonzero `kleinShift` (0/1/3) and nonzero slide
  (0/1/1.75 laps), this time testing the actual scenario. Two of *my own* methodology
  mistakes along the way, both now documented there in detail - a nearest-3D-neighbour
  search among seat *centres* is unreliable near the pinch (unrelated sheets of the
  self-intersecting immersion pass close to each other there), and comparing patch
  *corners* low-to-low instead of the correct low-to-high (a reflection reverses
  orientation within each cell - cell `f`'s low corner is the same point as cell
  `(f - 1)`'s high corner, not cell `f`'s partner's low corner) gives a meaningless
  ~1.3-1.6 world-unit "mismatch" at *any* pose, including the already-correct default.
  Once corrected: exhaustive nearest-neighbour search, every file's true topological
  partner (read from `SeamMap`, not assumed) is also the nearest one, at every
  `kleinShift`/slide combination tried, with only the deliberate `kCoverage` inter-square
  gap (~0.1-0.33 world units) as residual. This is now a permanent regression test:
  `a Klein seat's low corner is nearest its true partner's high corner, under any shift
  and slide` (`tests/render/test_play_surface.cpp`).
- **A direct visual check** (Claude, 2026-10-06): temporarily set the default `kleinShift`
  to `1`, rebuilt, captured `klein --geometry`, cropped in close on the pinch/seam area.
  No obvious break was visible, though the pinch region's self-intersection makes it
  genuinely hard to judge by eye alone from a single static angle.

So, as of `a1e6bda` ("re-verify the Klein seam with a corrected methodology; signed
kleinShift"): the geometry's cell-to-cell correspondence checks out numerically, at rest
and while sliding, with and without a square shift. **The report is not reproduced by any
check run so far.**

## A real bug, found but not landed (and not the reported symptom)

`PlaySurface::nearestBoundary` (`src/render/play_surface.cpp`) computes a glide
waypoint as `a.file + shortStep(...) * 0.5`, which for an adjacent seat is numerically
identical to seat `a`'s own centre coordinate in `pointAt`'s edge-at-integers convention -
so a piece's glide path currently hovers at the wrong point (the source seat's centre, not
the shared edge) mid-move. Confirmed independently by tracing `pointAt`'s coordinate
convention by hand, not just trusting the one-line fix.

**Why it is not landed:** the one-line `+ 0.5f` fix also changes the M17.19 anti-clip
look-ahead test's outcome - the buggy waypoint was (by accident) keeping a gliding piece
inside the tube on a torus rank-glide, which the M17.20 anti-clip choreography was
unknowingly compensating for. Fixing the waypoint without also revisiting the anti-clip
logic would be a regression there, not a clean fix. Needs its own spec.

**Why it is probably not the reported symptom either:** the seam rail (what a player
reads as "where the connection is") is coloured per-vertex on the same patch-corner
positions this file's corrected check already verifies; `nearestBoundary` is read only by
the piece-glide animation, a completely different code path. It could, however, explain a
*different* visual oddity - a piece visibly hovering in the wrong place mid-glide across
any seam, not specific to Klein or to a square shift - worth checking separately if it
turns out to be noticeable in play.

## What's needed to make progress

Every check so far has been **offline** (`PlaySurface::build` called directly, or a single
static `--shot`) - there is currently no way to drive `kleinShift` through a headless
capture (`--shot`/`--clip` always use default settings), and no synthetic-input tool to
script a live middle-drag session in this environment. The report is from live,
interactive play. To make real progress, one of:

1. **A specific, reproducible live-session description**: the exact `kleinShift` value,
   roughly how far (and which direction) the board had been dragged, and ideally a
   screenshot or screen recording of the actual moment the offset is visible - something
   to compare pixel-for-pixel against a fresh capture at the same pose, rather than a
   verbal description translated into a guess at what to check.
2. **A headless way to set `kleinShift` and `geometrySlideV` for a capture**, so this
   investigation can be driven by the same `--shot` tooling every other visual fix in this
   project uses, instead of hand-editing the default and rebuilding. Worth adding
   regardless of this specific bug, if more Klein-tuning work is expected.
3. **Chasing the camera angle instead.** The player's second comment ("might be camera
   bugs too") is completely uninvestigated - if the geometry is actually sound (as every
   check so far suggests), the camera - which frame it settles on, how it tracks a
   followed piece, whether a projection/rotation makes two genuinely-adjacent cells *look*
   half a square apart from certain angles - is the next most likely place to look, not
   another pass over the surface math.

Do not re-run the same offline geometric check a fourth time without new information - it
has now been done three times, by two different methods, and agrees with itself. If the
next pass finds the same "no bug" result, the bug (if real) is almost certainly not in
`PlaySurface`'s own corner/seat placement.
