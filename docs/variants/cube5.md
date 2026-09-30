# cube5 - three-dimensional chess

Definition: [`variants/cube5.toml`](../../variants/cube5.toml)

A 5×5×5 board. The piece set follows Raumschach (Maack, 1907) except in one place. The
**unicorn** travels the triagonals (`[1,1,1]`) and the **bishop** the planar diagonals
(12 directions in three dimensions); the **queen** and **king** are *line* movers - any
number of steps along one axis (6 directions) or two (12), and never three. The queen
therefore has 18 directions here, not 26: the cube diagonal is the unicorn's, and
folding it into the queen would be a different, stronger piece. This is the same
definition the queen and king have on every other board in the library, produced by
`straightLineAtoms` (`src/pieces/atom.hpp`) rather than written out by hand.

The historical Raumschach queen *does* combine rook, bishop and unicorn. ChessBox
deliberately does not, so that "queen" means one thing across the library and so the
mistake of silently inheriting a cube diagonal - which is invisible in two dimensions,
where `[1,1,1]` expands to nothing - cannot recur.

**The pawn rules and promotion are ChessBox's own choice, not a reconstruction.** A
pawn moves one step along the rank axis and captures on the forward diagonals - which
in three dimensions means four capture directions, two inside the level and two
between levels, from the same single `[1,1]` atom. A faithful Raumschach needs its
pawn rules taken from a primary source first; this variant does not claim to be one.

**Depth-1 count derived by hand before comparing with the engine:** 10 pawn pushes,
12 knight, 12 bishop, 8 unicorn and 10 queen moves, with both rooks and the king
completely blocked by their own army - 52 in total. Perft: 52 and 2665.
