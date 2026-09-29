# Compulsory captures

Definition: [`variants/mustcapture.toml`](../../variants/mustcapture.toml)

Standard chess in which you must capture if you can.

One rule, evaluated before legality:

```toml
when = "on_move_filter"
if = "(and (not is_capture) any_capture)"
effect: forbid_move
```

This is the mechanism a draughts-style game needs, demonstrated on a board whose other
rules are already verified. It is also the clearest example of the distinction between
*what the pieces can do* and *what the rules allow*: move generation still produces
every knight move, and the game layer is what removes them.

Rule syntax: [writing rules](rules.md).
