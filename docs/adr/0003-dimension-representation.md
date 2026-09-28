# ADR-0003: Runtime dimension count with a compile-time `kMaxDims = 8` budget

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
Dimensionality must be a *runtime* property: variants arrive from data files and
from Steam Workshop, so the shipped binary cannot know them at compile time. But
the innermost loop of move generation iterates axes, so a heap-allocated
coordinate would put an allocation and a pointer chase in the hottest code.

## Decision
- Coordinates are fixed-capacity PODs: `int16_t c[8]` plus an active-dimension
  count. No heap, trivially copyable, cache friendly.
- Dimension count is a runtime value; one binary handles 2-D..8-D.
- Hot kernels are templated on the dimension count and dispatched through a table
  built at variant load, so axis loops are fully unrolled.
- A dynamic implementation exists for `n > kMaxDims` for correctness only, and is
  differentially tested against the templated path.
- Movegen does not operate on coordinates at all; it operates on flat `CellId`s
  (ARCH §2). Coordinates exist for IO and UI.

## Consequences
Hard cap at 8 axes without a rebuild, which covers the spec's stated worst case
with room to spare; raising it is one constant. Some binary-size cost from seven
kernel instantiations. Dimension-lift invariance (M2.2) becomes a cheap, extremely
powerful test.

## Alternatives considered
- **Fully dynamic (`vector` per coordinate):** 5–20× slower in the inner loop and
  not fixable later.
- **Compile-time-only dimensions:** peak speed, but kills data-defined variants,
  which is the entire point of the project.

## How to reverse this
Raising the cap is trivial. Removing the cap entirely means keeping the dynamic
path and losing unrolling — a measured, deliberate trade, available at any time.
