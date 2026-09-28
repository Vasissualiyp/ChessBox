# ADR-0009: Differential testing against a naive oracle is the primary correctness method

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
The spec demands very heavy TDD with 100% of tests passing before each step. But
in a *generalized* engine, hand-written expected-value tests cover a vanishing
fraction of the input space: the space is (dimension count × extents × topology ×
piece atom sets × rule sets), and the bugs live in the interactions.

## Decision
Three mutually reinforcing mechanisms, in priority order:
1. **A naive oracle** (`tests/oracle/`): a deliberately slow, obviously-correct
   move generator — decode coordinates, brute-force every cell, analytic geometry,
   no tables, no bitsets. Every optimisation is proven equal to it by differential
   testing over randomly generated variants. The oracle is reviewed as carefully
   as production code and is never allowed to rot.
2. **Property tests over generated variants** (`RandomVariantFactory`), asserting
   invariants rather than values: make/unmake reversibility, incremental-vs-scratch
   hash equality, dimension-lift invariance, geometry transport closure, staged-gen
   completeness, table-vs-analytic equality.
3. **Golden corpora** for the things with external ground truth: standard chess
   perft to depth 6, Raumschach counts, the 5D-chess reference corpus, FEN
   round-trips, replay hashes.

A golden is never "updated" to make a build pass; a mismatch is investigated.

## Consequences
Bugs are found by the machine in the interaction space rather than by us in the
cases we happened to imagine. Costs: the oracle is real code to maintain, and CI
needs a meaningful time budget (mitigated by fast/nightly test tiers). Every
optimisation gets a cheap, decisive safety net, which is what makes an aggressive
performance strategy safe.

## Alternatives considered
- **Hand-written tests only:** insufficient coverage of the interaction space.
- **Fuzzing only:** finds crashes, not wrong answers, because there is no
  reference to compare against.

## How to reverse this
Nothing depends on it structurally; it is a discipline. Abandoning it would mean
accepting that generalization bugs ship.
