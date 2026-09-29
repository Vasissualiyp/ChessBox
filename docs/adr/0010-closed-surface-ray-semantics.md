# ADR-0010: Ray semantics on closed surfaces

- **Status:** Accepted
- **Date:** 2026-09-28

## Context

On a board with glued faces a ray does not simply run into a wall. Three problems
appeared as soon as the geometry layer met real move generation, all found by
property tests over random variants rather than by reasoning:

1. **An unlimited slider must terminate.** On a torus a rook's rank is a loop.
2. **A ray can cross the same cell twice** while travelling in different
   directions - a diagonal on a Klein bottle does - and the piece must not be
   offered that destination twice.
3. **Retracing a ray from its destination must return along the same path**, because
   the attack query walks backwards from a threatened cell. A first attempt ended a
   ray at its first repeated *cell*, which silently broke this: forward and backward
   walks then stopped in different places, and legality was wrong on exactly the
   boards hardest to reason about.

A fourth problem is deeper. When one step leaves the box on two axes at once, the
seam transforms are applied in axis order. For a torus or a Klein bottle the result
is the same either way, because leaving the min face and leaving the max face use
inverse transforms. But two gluings whose transforms disagree about a shared axis
give different answers depending on the order - and then no traversal rule can make
forward and backward agree, because the geometry itself is ambiguous at that corner.

## Decision

- **A ray ends when it returns to the state it started in**, `(cell, direction)`,
  with a generous step cap as a safety net. The step map is a bijection on states, so
  every orbit is a cycle through the start; being a cycle is what makes forward and
  backward traversal cover the same cells.
- **Duplicate destinations are removed from the move list**, not by cutting the ray
  short. Only boards with glued faces pay for this, and a box skips it entirely.
- **The fold order is part of the contract**: lowest axis index first.
- **`MoveGen::isAttacked` uses a forward scan on any glued board**, and keeps the
  fast backward walk for boxes - which is standard chess and every N-dimensional box
  variant. Correctness is not traded for the fast path on the boards where the fast
  path is unsound.
- **An oriented atom's threat span is its full unoriented expansion, per colour.** An
  orientation-reversing seam carries a forward direction out of the atom's own span,
  so a backward search restricted to that span misses pawn-like threats entirely; and
  the span is mirrored between the two sides, so it must be stored per colour.

## Consequences

Correct play on every topology the project ships, at a performance cost confined to
glued boards - where `isAttacked` is currently a full move scan. That is the single
largest known performance gap and it is written down as such.

The ambiguity is pinned by `tests/unit/geometry/test_corner_folds.cpp`, which asserts
both that stepping is reversible on every standard topology and that an ambiguous
gluing exists. If a future change makes the fold provably order-independent - or
rejects such gluings at validation time - that test fails, and the fast path can then
be enabled everywhere. It fails *because* the situation improved, which is the point.

## Alternatives considered

- **End a ray at its first repeated cell.** Simplest, and wrong: not symmetric under
  reversal (this is the bug that was actually shipped and then found).
- **Emit duplicates and let callers cope.** Perft would count one move twice.
- **Require identification transforms to commute.** Would reject the Klein bottle,
  whose generators famously do not commute and whose corner folds are nonetheless
  perfectly well defined.
- **Reject ambiguous gluings at load time.** The right long-term answer, deferred
  because the validation predicate needs care: checking every corner of a 7-axis
  board naively is too slow, and a cheap sufficient condition that still admits every
  interesting surface has not been worked out.

## How to reverse this

The traversal rule lives in `generateAtom` and the oracle's `ray`, which must always
change together. The `isAttacked` fallback is one condition.
