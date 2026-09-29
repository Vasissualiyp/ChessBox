# Charged pieces

Definition: [`variants/charged.toml`](../../variants/charged.toml)

A demonstration of custom data fields. Every piece carries a `charge` that increases
each time it moves, and a pawn that reaches three charges becomes a knight.

What it shows:

- A **piece field travels with its piece** — move a pawn and its charge moves with it,
  leaving the origin at the default.
- It is **part of the position's identity**: the hash changes when a charge does, so two
  positions that differ only in accumulated charge are different positions.
- It is **restored exactly by undo**, hash included.
- **Rule order matters.** The charge is incremented first and the threshold tested
  second, so a pawn transforms on the move that takes it to three, not the move after.
  Swapping the two `[[rule]]` blocks changes the game.

Declaring a field costs nothing for variants that do not: the columns are only allocated
when declared, and a field at its default contributes nothing to the hash.

Rule syntax: [writing rules](rules.md).
