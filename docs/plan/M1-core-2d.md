# M1 — 2D Generalized Core Engine (headless)

**Goal:** a headless engine whose *only* hardcoded assumption is "2 dimensions,
box boundary" — and even that is a value, not a compile-time constant. Standard
chess is reproduced exactly, as a data file, on top of fully general machinery.

**Why standard chess is the acceptance test:** it is the only variant in the
family with published, independently verified node counts. If the general engine
reproduces perft(6) = 119,060,324 from a TOML file, the generalized movegen is
correct to a degree no hand-written test set can establish.

**Exit condition:** all acceptance facts below hold; `ctest` fully green.

---

## M1.1 L1 space (test-first)

1. `DimSpec` construction from extents; stride computation; `cellCount`
   overflow detection (a variant declaring 1e12 cells must be a validation
   error, not a bad_alloc).
2. `Coord ⇄ CellId` round-trip. Property test over random `DimSpec`s: for every
   cell id, decode→encode is the identity, and every coordinate in range maps to
   a distinct id.
3. `Coord` is trivially copyable, ≤ 20 bytes, no heap (static asserts + tripwire).
4. Coordinate parsing/formatting (`a1`, `e4` for 2D; `(3,5,2)` general form) —
   in L9, but the round-trip test is written here against L1.

**Tests:** `tests/unit/space/*`; property tests over 200 random dim specs.

## M1.2 L2 geometry, box-only

Only the trivial topology in M1, but through the *final* interface so M3 adds
data, not code.

1. `Topology` interface: `step(CellId, DirId) -> StepResult`, `isInterior`,
   `neighbourCount`. Box topology = interior test + `kInvalidCell` off the edge.
2. Interior margin precomputation per direction; property test: a step is
   interior iff the decoded coordinate plus the direction vector is in range.
   This is the test that makes the M3 fast path trustworthy.
3. Differential test: `step` vs a naive decode-add-encode implementation over
   every (cell, dir) pair for boards up to 8×8 and all directions of the
   standard piece set — exhaustive, not sampled.

## M1.3 L3 position

1. `Piece` packing/unpacking (type, colour, flags) with exhaustive round-trip
   over all valid values.
2. Flat cell array; `pieceList` with canonical ordering; occupancy `BitWords`
   with the single-word 8×8 fast path. Invariant test: cells, pieceList and
   occupancy always agree (a `validate()` method asserted after every mutation
   in debug builds). **[INVARIANT]**
3. `make`/`unmake` for the general move type, with exact reversibility:
   property test over 10k random legal playouts asserts board, piece list,
   occupancy, state and Zobrist hash all return to their prior values.
4. Zobrist: incremental update equals from-scratch recomputation after every
   move (property test, every move, not sampled).
5. Field columns declared but empty in M1 (schema plumbing only), so M5 adds
   data, not structure.

## M1.4 L4 vector-move algebra — the centrepiece

This is where the spec's notation becomes code. Implemented strictly bottom-up.

1. **`MoveAtom` parsing and canonicalisation.** `[1,2,NULL]^1` and its TOML form
   both produce the same canonical atom. Magnitude multiset sorted; duplicate
   atoms deduplicated. Tests: canonical form is stable; `[2,1]` ≡ `[1,2]`.
2. **Expansion.** Given an atom and `D`, enumerate all injective magnitude→axis
   placements × all sign combinations, deduplicated, in a *canonical order*
   (determinism). Tests are table-driven against hand-computed counts:

   | atom | D=1 | D=2 | D=3 | D=4 |
   |---|---|---|---|---|
   | `[1]` | 2 | 4 | 6 | 8 |
   | `[1,1]` | 0 | 4 | 12 | 24 |
   | `[1,2]` | 0 | 8 | 24 | 48 |
   | `[1,1,1]` | 0 | 0 | 8 | 32 |
   | `[1,2,3]` | 0 | 0 | 48 | 192 |

   General formula asserted by a property test: for an atom with magnitude
   multiset of size `r` having duplicate-multiplicity groups `g1..gk`,
   `count = P(D,r)/∏(gi!) × 2^r`. A test computes it both ways and compares —
   this catches the classic duplicate-magnitude double-counting bug.
3. **Zero-magnitude and degenerate cases**: an atom with `r > D` expands to the
   empty set (a 3-D-only piece on a 2-D board simply cannot move that way) — an
   explicit, tested decision, not an error.
4. **`PieceType` tables**: atoms → contiguous `DirId` spans in the global
   direction table, plus per-atom `maxK`, `mode`, `capture`, flags.
5. **Modes**: `Slide` (repeat the atom step, blocked by occupancy), `Leap`
   (ignore intermediates), `Hop` (requires exactly one hurdle, for cannon-likes).
   Each has its own unit tests on hand-built positions.
6. **Riders vs leapers** fall out of `maxK`: nightrider = `[1,2]^inf`. Tested.

**Generalization check for M1:** a knight on a 4-D board is one TOML line and
*no* engine change. A test in M1 already asserts this by expanding standard
pieces at D=3 and D=4 even though no 3-D variant ships yet.

## M1.5 L5 movegen

1. **Naive oracle first.** Before the real generator: `tests/oracle/` — decode
   coordinates, brute-force all cells, no tables, no bitsets, obviously correct
   by inspection. Reviewed as carefully as production code. **[INVARIANT]**
2. Real generator per ARCH §7; every step differentially tested against the
   oracle on random positions of random variants.
3. **Pawns**, generalized: orientation axis + per-colour sign; quiet atom
   (`capture = Cannot`), capture atoms (`capture = Must`), first-move double
   step, en-passant target field, promotion region. Each as its own test group.
   The double-step's "passed-through" cell is computed via the geometry layer,
   so it will behave correctly on a cylinder in M3 for free.
4. **Castling** as a `CompoundMove` template: displacement list + conditions
   (`unmoved`, `pathEmpty`, `notAttacked`). Tests cover all standard cases plus
   the classic traps: castling out of/through/into check, rook attacked (legal),
   rights lost by rook capture, Chess960-style asymmetric placements.
5. **Attack queries**: `isAttacked(cell, byColour)` used for legality. Naive
   implementation first; the incremental version is M2+ and gated behind a
   differential test.
6. **Legality filter**: pseudo-legal → make → royal-attacked test → unmake.
   Royal piece(s) are *declared per variant*; zero-royal and multi-royal variants
   must not crash (tested), with an explicit policy for each.
7. **Staged generation**: captures / quiets / special, with a test that the union
   of stages equals the unstaged generation exactly, as a multiset.
8. **Perft**, with `perft-divide`, as an engine feature.

## M1.6 Golden verification (the hard gate)

Standard chess must match published counts **exactly**:

| Position | d1 | d2 | d3 | d4 | d5 | d6 |
|---|---|---|---|---|---|---|
| Initial | 20 | 400 | 8902 | 197281 | 4865609 | 119060324 |
| Kiwipete | 48 | 2039 | 97862 | 4085603 | 193690690 | — |
| Pos 3 | 14 | 191 | 2812 | 43238 | 674624 | 11030083 |
| Pos 4 | 6 | 264 | 9467 | 422333 | 15833292 | — |
| Pos 5 | 44 | 1486 | 62379 | 2103487 | 89941194 | — |
| Pos 6 | 46 | 2079 | 89890 | 3894594 | 164075551 | — |

CI runs the fast depths on every commit and the deep ones nightly.
**A perft golden is never "updated" to make a build pass.** **[INVARIANT]**

## M1.7 L8 game + L9 IO + CLI

1. `Game`: move history, undo/redo, threefold repetition (hash-based), 50-move
   rule as a *configurable* counter, insufficient-material as a variant-declared
   rule (not hardcoded — it is meaningless for most variants).
2. Adjudication: checkmate/stalemate/draw, with the stalemate *policy*
   configurable (loss/draw/win variants all exist).
3. **Variant TOML loader** with a real validator producing precise, user-facing
   error messages with line numbers. Every validation rule gets a test feeding
   malformed input. Fuzz target over the loader from day one.
4. **FEN-N**: a generalized position serialization that degenerates to standard
   FEN for 2-D chess (so standard FEN strings are accepted and emitted
   byte-identically — tested against a corpus). Round-trip property test for
   arbitrary variants.
5. Replay format + the replay corpus, hash-verified.
6. CLI: ASCII board render, move entry, `perft`, `divide`, `load <variant>`,
   `fen`, `undo`, plus a scriptable batch mode used by the golden tests.

## Generalization test

Adding a piece = TOML. Adding a variant = TOML + goldens. Adding a *dimension* =
M2, and M1 ends with the direction-expansion layer already dimension-agnostic and
tested at D=3/D=4, so M2 is plumbing, not redesign.

## Acceptance facts

1. All six perft positions match published counts at the depths listed.
2. Standard FEN in → identical FEN out, over a corpus of ≥ 1000 positions.
3. The optimised generator agrees with the naive oracle on ≥ 1e6 random
   positions across ≥ 100 randomly generated variants.
4. `make`/`unmake` is exactly reversible including hash, over ≥ 1e5 random moves.
5. Direction-expansion counts match the closed-form formula for all atoms with
   `r ≤ 4`, `D ≤ 8`.
6. Zero allocations occur during movegen (tripwire).
7. The engine contains no floating-point arithmetic.
8. A malformed variant file always yields a diagnostic, never a crash — asserted
   by a fuzz run of ≥ 1e6 cases.
9. Standard chess perft(5) throughput recorded as the M1 bench baseline.
