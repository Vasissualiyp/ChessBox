# Atomic chess on a torus

Definition: [`variants/atomic_torus.toml`](../../variants/atomic_torus.toml)

The atomic rules on a board with no edges — and the point is that it is not a feature.

Nobody wrote code to make an explosion wrap. The rule says "destroy the cells around
this one"; the geometry decides which cells those are; on a torus the cells around a4
include h4. Rules and geometry are orthogonal, and this variant exists to hold that
claim to a test.

As with the plain torus there is no promotion (no last rank) and no castling (the files
wrap), and the armies start four ranks apart because the usual array puts them in
contact.

Rule syntax: [writing rules](rules.md).
