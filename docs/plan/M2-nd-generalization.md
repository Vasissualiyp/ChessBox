# M2 — N-Dimensional Generalization

**Goal:** the same engine, same data files, arbitrary dimension count, with no
performance cliff at D=2 and no correctness cliff at D=8.

**Exit condition:** 2-D perft goldens unchanged and no slower than the M1
baseline; 3-D/4-D variants ship with their own goldens; dimension-lift invariance
holds as a property test.

## M2.1 Dimension dispatch

- Hot kernels (`step`, ray walk, coordinate decode, interior test) templated on
  `Dims` and instantiated for 2..8, selected by a dispatch table built at
  variant load. Loops over axes become fully unrolled and vectorizable.
- A single dynamic implementation covers `n > kMaxDims`, exists for correctness
  only, and is differentially tested against the templated one.
- **Test:** for each D in 2..8, the templated and dynamic paths produce identical
  move lists on random positions. **[INVARIANT]**
- **Bench:** D=2 must not regress versus M1 (this is the whole point of dispatch,
  and it is a gate, not a hope).

## M2.2 Dimension-lift invariance — the key correctness idea

Embedding a D-dimensional variant into D+1 dimensions with extent 1 on the new
axis must produce an *identical game*: identical legal-move counts at every node,
identical perft, identical result. Any violation is a generalization bug.

- Property test: for every shipped variant and every D' > D up to 8, `perft(k)`
  is equal after lifting, for k as deep as the time budget allows.
- This single test catches nearly every class of dimension-handling error
  (off-by-one in expansion, sign handling, stride math, orientation axis choice).
  **[INVARIANT]**

## M2.3 Combinatorial budgets

Direction counts grow fast (`[1,2,3]` at D=8 is 8·7·6·2³ = 2688 directions).

- `VariantSpec` load computes total direction count and rejects/warns past a
  configurable budget with an actionable message.
- Atom order `r` capped per variant; a warning path for expensive atoms.
- **Tests:** asserted table sizes for a matrix of (atom, D); a test that the
  budget error message names the offending piece and atom.

## M2.4 Shipped N-D variants

Each with a TOML file, docs page, starting-position golden, and perft goldens:

- **Raumschach** (5×5×5, Maack 1907 — the classical 3-D chess) chosen first
  because its rules are historically fixed and widely documented, so the piece
  definitions are not ours to invent. Its perft counts are *self*-generated (no
  authoritative published corpus is known to exist), so they are validated against
  the naive oracle rather than against an external reference — a weaker gate than
  M1's, which is exactly why M1's standard-chess perft gate must come first.
- **3-D 8×8×8** sandbox variant with a declared piece set.
- **4-D 4×4×4×4** variant, chosen small so perft depth is meaningful.
- **Lifted standard chess** (8×8×1×1) as the invariance witness.

## M2.5 TUI multi-slice viewer (the early-visibility escape hatch)

A terminal viewer that renders any N-D board as a laid-out grid of 2-D slices
(exactly the projection model the Vulkan renderer will use in M4, minus the
pixels). Cheap, fully testable via golden text output, and enough to play 3-D and
4-D games by hand before M4 exists. Its projection logic is the *same code* the
renderer will consume, so M4 inherits tested behaviour.

## Generalization test

Adding the (n+1)-th dimension to a variant is an extents change in TOML. Adding
support past `kMaxDims` is one constant plus a rebuild.

## Acceptance facts

1. 2-D perft goldens byte-identical to M1; NPS within noise of the M1 baseline.
2. Dimension-lift invariance holds for every shipped variant, D up to 8.
3. Templated and dynamic paths agree on ≥ 1e6 random positions per D.
4. Raumschach move counts agree with the naive oracle at every depth the time budget allows, and its rules are documented against the historical source.
5. The direction-budget error path is tested and actionable.
6. The TUI viewer's output is golden-tested for 2-D, 3-D and 4-D boards.
