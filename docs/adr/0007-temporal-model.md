# ADR-0007: Time travel as pluggable policies over extra lattice axes

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
The spec asks for faithful *5D Chess With Multiverse Time Travel* semantics now,
and explicitly anticipates later generalizations: toroidal timelines, multiple
multiverse dimensions. Purpose-built time-travel code would satisfy the first and
obstruct the second; a fully abstract multiverse framework risks getting the
reference rules subtly wrong while chasing generality.

## Decision
Turn (`t`) and timeline (`l`) are ordinary lattice axes tagged with an
`AxisKind`, so move generation and geometry treat them exactly like spatial axes
— a time jump is simply a direction with a nonzero `t` component. The
5D-chess-specific rules live entirely in a small set of pluggable policies:
`PresentPolicy`, `BranchPolicy`, `TurnPolicy`, `ArrivalPolicy`, `CheckPolicy`.
Policies are written against *the set of* temporal/multiverse axes, never a single
`l`. Faithfulness is enforced by a transcribed reference corpus written before
implementation (M6.0).

## Consequences
Toroidal timelines become a boundary identification (ADR-0004); a second
multiverse axis becomes an axis declaration. Cross-board check falls out of
ordinary attack generation because all boards are slices of one lattice. The cost:
the lattice is large, so slice-local occupancy and incremental attack maps become
mandatory rather than optional, and `TurnPolicy` (atomic multi-board submission)
is the most intricate state machine in the project.

## Alternatives considered
- **Purpose-built 5D chess:** simpler to get exactly right, but generalizing later
  means a refactor of the most rule-dense subsystem.
- **Fully general N-axis multiverse from the start:** maximal generality with a
  high risk of subtly mis-implementing the known-good reference rules, which are
  the only thing we can validate against.

## How to reverse this
Policies are interfaces; a specialised fast path for the standard preset can be
added behind them without changing the model.
