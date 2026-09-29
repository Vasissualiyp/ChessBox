# cube5 - three-dimensional chess

Definition: [`variants/cube5.toml`](../../variants/cube5.toml)

A 5×5×5 board. The piece set follows Raumschach (Maack, 1907), whose movement rules
are historically settled: the **unicorn** travels the triagonals (`[1,1,1]`), the
bishop the planar diagonals (12 directions in three dimensions), and the queen
combines rook, bishop and unicorn for 26 directions.

**The pawn rules and promotion are ChessBox's own choice, not a reconstruction.** A
pawn moves one step along the rank axis and captures on the forward diagonals - which
in three dimensions means four capture directions, two inside the level and two
between levels, from the same single `[1,1]` atom. A faithful Raumschach needs its
pawn rules taken from a primary source first; this variant does not claim to be one.

**Depth-1 count derived by hand before comparing with the engine:** 10 pawn pushes,
12 knight, 12 bishop, 8 unicorn and 14 queen moves, with both rooks and the king
completely blocked by their own army - 56 in total. Perft: 56 and 3095.
