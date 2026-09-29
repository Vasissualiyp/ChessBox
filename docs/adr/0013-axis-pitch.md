# ADR-0013 — An axis may be sampled more finely than a piece moves along it

**Status:** Accepted

## Context

A move atom's magnitudes are *units of movement*: a knight's `[1,2]` means one step on
one axis and two on another. Every axis so far has had one cell per unit, so a direction
and a lattice offset were the same thing and nothing had to say so.

The turn axis is not like that. Every half-move appends a board, so boards along it
alternate whose move it is. Board `t` and board `t+1` are the same game one ply apart,
and a piece may only ever stand on a board where it is its own side to move. One *turn*
of travel is therefore two boards, not one.

Treating the turn axis like any other produced a real bug, reported from a game: a knight
on a board at turn 5 — one White had just moved on, so Black to move — offered a move to
turn 4, a board on which it was White's turn and which Black may not play on at all. The
move the player was actually trying to make, one turn back to turn 3, was not offered.

Three fixes were available.

1. **Per-piece**: let an atom name a magnitude of 2 for the turn axis. Every piece that
   travels in time would have to say so, every variant author could get it wrong, and the
   atom algebra would stop being about axis-independent magnitudes.
2. **In the engine**: have movegen double any step on an axis declared `temporal`. That is
   the engine knowing what time *is*, which ADR-0007 exists to prevent.
3. **Per-axis, as data.** The property belongs to the axis: *every* piece crosses this
   axis two boards at a time, whatever piece it is.

## Decision

`AxisDecl` carries a `pitch` — how many lattice cells one unit of movement along that axis
covers. It defaults to 1, so every existing variant is unchanged and unchanged in meaning.
`variants/5d.toml` declares `pitch = 2` on its turn axis and nothing on its line axis,
because a timeline hop keeps the turn and therefore keeps whose move it is.

The scaling is applied **once**, where the direction table is built. Atom expansion stays
pure combinatorics over magnitudes and axes; geometry, the ray walk, transport and every
layer below the variant go on seeing directions in lattice cells, which is the only unit
they have ever cared about. The naive oracle applies the same rule from its own code, so
the differential test still compares two independent implementations rather than one.

## Consequences

- A knight travelling one turn back lands two boards back, on a board it may move on.
- A slider through time steps two boards at a time and is blocked by pieces on the boards
  between - which is the reference game's behaviour.
- A variant may now describe an axis sampled at any rate. Nothing else uses it yet, and
  nothing should unless the axis genuinely has more cells than it has positions.
- `pitch` is validated at load: it must be at least one cell.
- It is *not* inferred from `AxisKind::Temporal`. A variant that wanted both players to
  move from the same board, or three-phase turns, would say so in its own file rather
  than fight a rule the engine had assumed.

## Not decided here

Whether a move through time may go *forward*, and which boards exist at all, are the
multiverse layer's business (`src/temporal/`), not the lattice's. Pitch only fixes the
size of a step.
