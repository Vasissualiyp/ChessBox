# Writing rules

A variant's rules are data. The engine has no code for atomic chess, for forced
captures, or for pieces that charge up as they move — each is a `[[rule]]` block in a
TOML file, interpreted by a small VM (ADR-0005).

## Shape

```toml
[[rule]]
name = "atomic capture"      # used in error messages and in the rule trace
when = "on_capture"          # the trigger
if = "(ne (type_at move.to) piece:pawn)"   # optional condition

  [[rule.effect]]
  op = "destroy_region"
  at = "move.to"
  radius = 1
  affects = "(ne (type_at cell) piece:pawn)"
```

Rules run in declaration order, and that order is part of the variant's meaning — in
`charged.toml` the charge is incremented before the threshold is tested, and swapping
the two rules changes the game.

## Triggers

| `when` | Fires | Notes |
|---|---|---|
| `on_move_filter` | before legality | the only place `forbid_move` means anything |
| `on_capture` | a piece was taken | the move is already on the board |
| `on_move_end` | after the move, before the turn passes | |
| `on_turn_end` | after the turn passes | |
| `on_result_query` | when the result is computed | |

## Effects

| `op` | Fields | Does |
|---|---|---|
| `destroy` | `at` | removes whatever is on that cell |
| `destroy_region` | `at`, `radius`, `affects` | removes pieces in a Chebyshev neighbourhood; `affects` selects which |
| `transform` | `at`, `piece` | changes the piece's type in place |
| `spawn` | `at`, `piece`, `color` | puts a piece on a cell |
| `set_piece_field` | `at`, `field`, `value` | writes a per-piece field |
| `set_cell_field` | `at`, `field`, `value` | writes a per-cell field |
| `forbid_move` | — | vetoes the candidate move |
| `repeat_turn` | — | the mover moves again |
| `end_game` | `outcome` | `mover_wins`, `mover_loses` or `draw` |

`radius` is capped at 4, because a radius of `r` visits `(2r+1)^dims` cells and that
grows very fast on a 7-axis board.

## Expressions

S-expressions, so there is no precedence to get wrong and an error can point at a token.

```
(and (is_capture) (eq (coord rank move.to) 7))
(gt (piece_field charge move.to) 2)
(ne (type_at cell) piece:pawn)
```

**Values:** `move.from`, `move.to`, `move.capture_cell`, `mover.color`, `mover.type`,
`is_capture`, `side_to_move`, `halfmove_clock`, `any_capture`, and `cell` — which is
only bound inside an `affects` filter, because only there is there a cell being walked.

**Operators:** `not and or eq ne lt gt add sub`, `type_at`, `color_at`, `is_empty`,
`has_capture_from`, and three that take a name first: `(piece_field <name> <cell>)`,
`(cell_field <name> <cell>)`, `(coord <axis> <cell>)`.

**References:** `piece:queen` becomes a type id, `color:white` a colour. A name that
does not exist is a load error saying so, never a silent zero.

## Custom fields

```toml
[[piece_field]]
name = "charge"
default = 0
min = 0
max = 8
hashed = true     # part of the position's identity; set false for cosmetic state
```

A piece field travels with its piece and returns to its default when the piece leaves.
A cell field stays where it is. Both are columns: a variant declaring none allocates
nothing, and a field sitting at its default contributes nothing to the hash, so a board
full of defaults hashes exactly like a board with no fields at all.

## What the VM guarantees

- **It cannot hang.** Expressions are trees with no loops or jumps, and the one
  iterating effect walks a bounded neighbourhood. The step budget bounds cost, not
  termination.
- **It cannot execute code.** There is no scripting runtime and no I/O — which is what
  makes a variant safe to load from a stranger (ADR-0005).
- **It is deterministic.** No floating point, no hash-container iteration, fixed rule
  order. Multiplayer and replay compare position hashes, so this is not optional.
- **It is reversible.** Every change an effect makes goes through the undo record, so
  a rule-driven explosion undoes exactly, hash included. Asserted by test.

## Rules change legality, not just consequences

This is the part worth understanding. In atomic chess, a capture that would destroy
your own king is illegal — and nothing in `atomic.toml` says so. Legality is decided
*after* a move's effects run: the move is played, its rules fire, and only then is the
royal piece checked. A move that blows up your own king simply never appears among the
legal moves, and one that blows up the enemy's leaves them with no legal moves at all.

The cost is that a rule-carrying variant runs its effects once per candidate move, so
move generation is slower than for a plain variant. That is recorded as the known price
of the design rather than hidden.

## What is not expressible yet

- **Multi-jump sequences** (a draughts capture chain). `repeat_turn` exists and
  `has_capture_from` exists, but no shipped variant combines them, so consider that
  path untested.
- **Superposition** (quantum chess). Not a gap in the rule catalogue — see
  [ADR-0012](../adr/0012-quantum-chess.md).
