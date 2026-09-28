# M6 — Temporal / Multiverse (5D Chess)

**Goal:** faithful *5D Chess With Multiverse Time Travel* semantics, implemented
as policies over extra axes so that toroidal timelines and multiple multiverse
dimensions are configuration (ADR-0007).

**Exit condition:** the transcribed reference corpus reproduces exactly, and a
"5D chess on a 5-torus" variant (7 axes) loads and plays.

## M6.0 Corpus first (before any implementation) **[INVARIANT]**

Transcribe from the reference game, *before writing code*: starting positions for
each of its board presets, a set of known puzzles with their solutions, a set of
positions with known legal-move counts, and a set of completed games as replays.
This corpus is the specification. It is the only defence against getting
time-travel rules subtly wrong, which is the dominant risk of this milestone.

## M6.1 Axis semantics

- `t` (turn/time) and `l` (timeline/multiverse) axes declared with
  `AxisKind::Temporal` / `Multiverse`. Movegen already treats them as ordinary
  axes — a time jump is a direction with a nonzero `t` component. Nothing in
  L4/L5 changes; this is the payoff of ARCH §3.
- Boards are the (t, l) slices of the single lattice, not separate objects. This
  is what makes cross-board check fall out of ordinary attack generation.
- Sparse timelines: the `l` extent is a capacity; an allocation map tracks which
  timelines exist. Test: memory grows with *active* timelines, not capacity.

## M6.2 Policies (each pluggable, each individually tested)

- **PresentPolicy** — computes the present line (the minimum-`t` frontier over
  active timelines, per the reference rules) and hence which boards must receive
  a move this turn. Tested against corpus positions where the present is
  non-obvious.
- **BranchPolicy** — a move landing on a past board creates a new timeline;
  timeline numbering/sign conventions must match the reference exactly (this is
  a classic source of mismatch). Tested against corpus branch sequences.
- **TurnPolicy** — multi-board turn submission: a turn is a *set* of moves, one
  per present board, submitted and committed atomically, with partial-submission
  state and legality of the whole set (a submission illegal only in combination
  must be rejected). This is the hardest rule in the project; it gets its own
  test file and its own state machine diagram in the docs.
- **ArrivalPolicy** — what a piece does on arriving, and the "inactive timeline"
  rules.
- **CheckPolicy** — check/checkmate across boards and timelines, including the
  rule that a player must resolve check on every board.

## M6.3 Performance

- The lattice is large (boards × timelines × turns). Occupancy bitsets and
  attack queries must be per-slice with a slice index, so a move on one board
  does not rescan the multiverse.
- Incremental attack maps become mandatory here rather than optional; introduced
  behind the oracle differential test as always.
- Move submission is the search unit for M9, so the API is designed for batched
  make/unmake of move *sets* with exact reversibility.
- **Bench:** node throughput on the reference presets; a documented target and a
  regression gate.

## M6.4 Generalization deliverables

Configuration-only, each with tests, proving the framework claim:

- **Toroidal timeline**: a periodic identification on the `l` axis (M3 machinery).
- **Two multiverse axes**: `l1`, `l2` with the same policies — the policies are
  written against "the set of multiverse axes", not against a single `l`.
- **5D chess on a 5-torus**: 5 spatial + `t` + `l` = 7 axes, all spatial axes
  periodic. This is the spec's stated worst case; it must *load and play*, with a
  documented (possibly modest) performance figure rather than a promise.

## Generalization test

Adding a multiverse dimension is an axis declaration. Making a timeline toroidal
is an identification block.

## Acceptance facts

1. Every corpus position's legal-move count and every puzzle solution reproduces
   exactly; every corpus replay replays to the same final hash.
2. Timeline numbering matches the reference convention exactly.
3. Multi-board turn submission rejects combination-illegal sets; state machine
   fully covered.
4. Cross-board check and checkmate are correct on the corpus.
5. Memory scales with active timelines, not declared capacity.
6. The 7-axis torus variant loads and plays; its throughput is measured and
   documented.
7. Dimension-lift invariance (M2.2) still holds with temporal axes present.
