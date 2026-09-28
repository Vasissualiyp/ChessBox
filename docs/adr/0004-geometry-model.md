# ADR-0004: Boundary geometry as an analytic transition group with boundary-only transport tables

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
The project must support cylinders, tori, Möbius bands, Klein bottles, projective
planes, reflecting walls, and their higher-dimensional analogues — declared by
users. Two traps: (1) a piece crossing an orientation-reversing seam must have its
*direction* transported, not just its position, or the board is silently wrong;
(2) a full `cells × directions` neighbour table is gigabytes for a 7-axis board.

## Decision
- Topology is declared as a set of face identifications, each with an affine
  coordinate transform. The action on direction vectors is *derived* from the
  transform's linear part, so authors cannot get transport wrong.
- The identification set generates a finite transition group, validated at load.
- Hot path: an interior test (precomputed per-direction margins) plus a plain
  integer stride addition. Only boundary-crossing steps consult transport.
- Transport is a table built for boundary cells only, with an analytic fallback
  when a variant would exceed the memory budget. Both paths are differentially
  tested against each other.

## Consequences
Ordinary box boards pay essentially nothing. Memory is proportional to boundary
size, not volume. New topologies are data. Non-orientable correctness is testable
against mathematical invariants (M3.3) rather than hand-written move lists.
Degenerate identifications need explicit canonicalisation and validation.

## Alternatives considered
- **Full precomputed neighbour tables:** fastest, but memory-infeasible in high
  dimensions and requires bespoke code per topology.
- **Pure analytic, no tables:** no memory cost but branchy math in the hottest
  loop, and no cheap interior fast path.

## How to reverse this
The `Topology::step` interface hides the choice entirely; either path can be
swapped out without touching movegen.
