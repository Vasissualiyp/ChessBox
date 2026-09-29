# Shipped variants

Every file in `variants/` is a complete game definition in data - no engine code is
specific to any of them. All are covered by
`tests/golden/test_shipped_variants.cpp`, which finds them by scanning the directory,
so a new variant is held to the same bar without being registered anywhere.

| Variant | Board | Geometry | Notable |
|---|---|---|---|
| [standard](standard.md) | 8×8 | box | the reference; reproduces published perft to depth 6 |
| [cylinder](cylinder.md) | 8×8 | files glued | pawns, promotion and castling all survive |
| [torus](torus.md) | 8×8 | both axes glued | no edges; pawns but no promotion |
| [mobius](mobius.md) | 8×8 | files glued with a twist | non-orientable; no pawns |
| [klein](klein.md) | 8×8 | Klein bottle | bishops are not colour-bound |
| [mirrorbox](mirrorbox.md) | 8×8 | reflecting file walls | 64 distinct cells; rays bounce |
| [cube5](cube5.md) | 5×5×5 | box | unicorns; three-dimensional play |
| [hyper4](hyper4.md) | 4×4×4×4 | box | 48-direction knights |
| [torus3d](torus3d.md) | 4×4×4 | all three axes glued | fully uniform 3-D board |
| [5d](5d.md) | 8×8×3×2 | box | a turn axis and a timeline axis; pieces move through time |
| [atomic](atomic.md) | 8×8 | box | captures destroy their neighbourhood |
| [atomic_torus](atomic_torus.md) | 8×8 | torus | the same rules, wrapping — rules × geometry |
| [mustcapture](mustcapture.md) | 8×8 | box | captures are compulsory |
| [charged](charged.md) | 8×8 | box | custom per-piece fields |

Rules and custom fields are documented in [writing rules](rules.md).

## What every variant page records

The rules, where they came from, and - importantly - **which rules are ChessBox's own
choice rather than an established definition**. Node counts in these pages and in the
goldens are this engine's own output except for standard chess; they are validated
against the naive oracle, and their job is to notice change rather than to prove
correctness. See ADR-0009.

## The recurring design question

Several familiar rules stop being well defined once the board is glued:

- **"Forward"** needs a globally consistent orientation. A torus has one; a Möbius
  band and a Klein bottle do not. The variants here answer this by simply not
  fielding oriented pieces on non-orientable boards, which is a choice, not a law -
  see `docs/plan/M3-geometry.md` section 3.4 for the options.
- **"The last rank"** does not exist when the rank axis is a loop, so a torus
  declares no promotion region and its pawns never promote.
- **"Between the king and the rook"** is ambiguous once the files wrap, so the glued
  variants drop castling.
- **Bishop colour binding** is a theorem about orientable boards only. On a Klein
  bottle a bishop reaches every cell, which changes material value and is asserted
  as a test because it is a sharp check on direction transport.
- **The opening array itself.** The ordinary chess setup is unplayable on a torus:
  rank 1 and rank 8 are adjacent, so the armies start in contact and White is already
  in an inescapable check. Two shipped variants had exactly that bug before
  `test_shipped_variants.cpp` began asserting that the side to move has a legal move.
