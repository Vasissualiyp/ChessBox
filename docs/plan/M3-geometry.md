# M3 — Boundary Geometry

**Goal:** arbitrary finite boundary topology, declared as data, correct for
non-orientable surfaces, with no cost to ordinary box boards.

**Exit condition:** the topology family below all play correctly, with
mathematically meaningful invariants tested (not just "it did not crash").

## M3.1 Identification language

```toml
[[geometry.identify]]
face = "x=max"; to = "x=min"                    # periodic
[[geometry.identify]]
face = "y=max"; to = "y=min"; map = "x -> W-1-x" # Klein seam
[[geometry.identify]]
face = "x=max"; to = "x=max"; kind = "mirror"    # reflecting wall
```

- Parsed into `(faceA, faceB, affine transform)`; the induced direction action is
  *derived*, not authored, from the transform's linear part — so an author cannot
  get the direction transport wrong. **[INVARIANT]**
- The identification set generates a transition group; the loader computes its
  closure and validates consistency (no contradictory identifications, no
  cell mapped to two distinct classes).
- **Tests:** group closure is finite and correct for each shipped topology;
  transforms compose associatively; each seam crossing is an involution where it
  should be.

## M3.2 Hot path

Implements ARCH §4.2: interior stride fast path, boundary transport tables built
for boundary cells only, analytic fallback past a memory budget.

- **Tests:** table path ≡ analytic path exhaustively for small boards, sampled
  for large ones; memory usage asserted against a formula; the budget fallback is
  exercised by a deliberately oversized variant.
- **Bench:** a box board must be no slower than in M2 (the interior test must not
  cost anything real).

## M3.3 Topology family (each: TOML + docs + goldens + skill checklist)

| Topology | Identifications | Invariant tested |
|---|---|---|
| Box | none | baseline |
| Cylinder | x periodic | rook orbit on a rank has length W |
| Torus | x,y periodic | every cell has the same neighbour count; bishop orbit lengths match the lattice's closed-geodesic formula |
| Möbius band | x periodic with y-flip | orientation reversal after one circuit: a bishop returns to its start on the *opposite* diagonal colour |
| Klein bottle | x periodic, y periodic with x-flip | no global colouring exists: the bishop's reachable set is the whole board (a strong, sharp fingerprint) |
| Real projective plane | antipodal identification | each cell class has exactly the predicted size |
| Reflecting walls | mirror faces | ray path length matches the unfolded reflection |
| N-D torus | all axes periodic | neighbour-count uniformity at D=3..7 |
| 3-D Klein | mixed periodic/flip | direction transport closure; orbit goldens |
| Mixed | e.g. x periodic, y mirrored, z open | per-axis independence |

The "invariant tested" column matters: these are *mathematical* facts about each
surface, so they detect subtly wrong transport that a hand-written expected-move
list would miss. **[INVARIANT]**

## M3.4 Rules that need a policy on exotic surfaces

Non-orientable boards make several standard rules genuinely ambiguous. Each
becomes an explicit, documented, per-variant policy with tests for every option:

- **Pawn direction** after crossing an orientation-reversing seam (keep global
  axis sign / follow transported direction / forbid crossing).
- **Promotion regions** when there is no "last rank" (region declared explicitly
  by predicate; a torus variant may have none).
- **Castling** when the path wraps (allow / forbid; and whether wrapping counts
  as "between").
- **En passant** when the passed cell is reached by two different transported
  paths.
- **Bishop colour-binding** loss, which changes material evaluation — recorded
  for M9, and surfaced in the variant docs.

## M3.5 Renderer metadata

Geometry exports seam descriptors (which faces glue to which, with what
transform) for M4's seam visualisation and wrap-preview rendering. Defined here,
consumed there, tested here by asserting the descriptor round-trips and matches
the transition group.

## Generalization test

Adding a topology is a `[[geometry.identify]]` block plus goldens. No engine code.

## Acceptance facts

1. Every topology in the M3.3 table plays legally, with its listed invariant
   asserted by a named test.
2. Table and analytic transport agree; the budget fallback is exercised.
3. Box-board performance unchanged from M2.
4. Each exotic-surface rule policy has tests for every option.
5. Dimension-lift invariance (M2.2) still holds, including for periodic axes.
6. A contradictory identification set yields a precise validation error.
