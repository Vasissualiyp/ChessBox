# M10 — Per-Variant Trainable AI

**Goal:** a competent opponent for *any* variant, including ones the developers
never saw. This is a research milestone; the plan commits to the infrastructure
and to an honest evaluation methodology, not to a strength number.

## M10.1 Why this is hard here

Every standard chess engine assumption is unavailable: no handcrafted piece
values (pieces are user-defined), no known board size, no colour-binding
heuristics (Klein bottles destroy them), no opening book, no endgame tables, and
a branching factor that can be enormous (5-D chess move sets are huge). So the
architecture must *derive* its knowledge from the variant definition and from
self-play, and must degrade gracefully.

## M10.2 Search

- Generic alpha-beta with iterative deepening, transposition table (Zobrist from
  ARCH §11), move ordering, quiescence over "forcing" moves — where "forcing" is
  derived from the variant (captures, checks, and VM-declared forcing effects)
  rather than hardcoded.
- MCTS/PUCT as an alternative backend, selected per variant, because enormous
  branching factors favour it. Both backends share the same move generator and
  are compared on the same benchmark set.
- Search operates on move *sets* where the variant demands it (M6), so the
  temporal case is not a special case bolted on.
- **Tests:** search returns a legal move for every shipped variant; fixed-depth
  search results are deterministic and reproducible; no allocation in the inner
  loop; mate-in-N puzzle suites per variant.

## M10.3 Automatic baseline evaluation

Before any learning: a *derived* evaluator computed from the variant spec —
mobility-based piece values estimated by Monte-Carlo sampling of each piece
type's average move count on random positions of that variant's geometry, plus
royal safety, plus goal-distance terms for `OnEnterRegion` win conditions. This
alone gives a playable opponent for a brand-new variant with zero training, and
it is the baseline every learned model must beat.

## M10.4 Learning

- NN input encoding derived from the variant (cell planes per piece type per
  colour, field planes, geometry-aware adjacency), with a fixed maximum shape so
  one architecture serves many variants.
- Self-play pipeline: generation, training, gating (a new model must beat the
  current one over a statistically significant match before promotion).
- Inference in-engine: a small dependency-free int8/int16 quantized evaluator so
  the shipped binary needs no ML runtime, floats confined to the (isolated,
  non-core) trainer. **[INVARIANT]**
- Reproducibility: seeds, data provenance, model hashes recorded; a model is
  identified by `(VariantId, modelHash)`.

## M10.5 Evaluation methodology

- Per-variant Elo ladders versus the derived baseline and versus fixed-strength
  reference opponents; for standard chess, versus a published engine at limited
  depth as an external sanity check.
- Published results include hardware, time control, and confidence intervals.
  No strength claims without them. **[INVARIANT]**

## Acceptance facts

1. Every shipped variant gets a legal-playing opponent with zero training.
2. Standard-chess search passes a mate-in-N suite and reaches a documented,
   measured strength at a fixed time control.
3. The learning pipeline promotes a model only through a gated match.
4. Inference adds no float arithmetic to the engine core and no new runtime deps.
5. All results are reported with hardware, time control, and CIs.
