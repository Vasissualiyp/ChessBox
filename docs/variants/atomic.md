# Atomic chess

Definition: [`variants/atomic.toml`](../../variants/atomic.toml)

A capture destroys the capturing piece, the captured piece, and every non-pawn in the
eight surrounding cells. Pawns are immune, which is what the effect's `affects` filter
says.

The whole variant is three effects appended to the standard piece set. Nothing else
differs, and nothing in the engine knows the word "atomic".

**The part worth noticing:** nothing declares that blowing up a king wins or loses.
Legality is decided after a move's effects run, so a capture that destroys your own king
simply is not a legal move, and one that destroys the enemy's leaves them with no legal
moves at all — which the existing adjudication already reports as a win. A rule about
explosions produced a rule about winning, for free.

Rule syntax: [writing rules](rules.md).
