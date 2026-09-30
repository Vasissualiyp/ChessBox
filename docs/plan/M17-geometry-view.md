# M17 — Play on the shape: the geometry view

**Goal.** The board stops being a *representation* of a topology on a square grid and
becomes the topology. Press a button and the flat 8×8 becomes the object the variant is
actually played on - the files roll into a cylinder, the cylinder closes into a torus, the
rank edges pinch into a Möbius band or a Klein bottle, a mirror box grows its silvered
walls - and you keep playing on it. Turn it off and it lies flat again. The shapes already
exist for the New Game screen (`render/overture_scene.cpp`); this makes the *play* board
be the same shape, so "the a-file and the h-file are the same edge" is not a cyan line you
have to believe but a place you can point at.

**Exit condition.** On any shipped glued variant, a button (and a key) morphs the play
board into its own embedding, keeps the position and the move valid on it, lets a move be
made by picking a cell on the surface, and morphs back to the flat board; `standard` and
every non-glued variant are unchanged.

---

## M17.0 Why this is not the overture

The overture animates a fixed, authored scene: a separate 8×8 built for the menu, no
`Session`, no legal moves, no picking. M17 draws the *live* position on the warped surface.
The two share the warps and nothing else, and the separation is what keeps the overture
cheap and the game honest: M17 must never invent a move or a cell, and the overture must
never read the engine.

Depends only on M3 (the identification model), M4 (the renderer and `view::layout`) and the
M13.2 warps, all done. It is **Wave 1, before M16.1** in the release sequence, because it is
the most clippable thing the game can do with a board - a trailer that shows chess being
played *on a donut* is the store page's whole argument, and it needs no opponent and no
campaign.

---

## M17.1 The surface, from the variant

`app::overtureSignature` (M13.1) already turns a resolved spec into a `SurfaceKind`. M17
reuses it: the play board's embedding is `render::derivedSurfaceAt(kind, u, v)` (M13.2),
the exact surfaces the library screen already draws, asserted pointwise-equal to the
hand-authored ones. No new geometry; a variant that glues two axes is a torus whether it is
in a menu or a game.

## M17.2 The cell on the surface

- Each cell's centre is placed by the surface at its lattice `(u, v)`, exactly as the
  overture places its cells; its four corners come from `(u, v)` at the cell's own corners,
  so a cell bends with the surface instead of floating over it.
- Pieces ride their cell's normal (the finite-difference normal the overture uses), so a
  rook on the far side of a torus stands up outwards, not into the surface.
- The flat board is `t → 0`; the morph is the same blend the overture uses, driven by a
  UI clock rather than by the overture player.

## M17.3 Playing on it

This is the part that is engine work, and the part to get right:

- **Picking.** `Session::clickPixel` currently ray-casts against the axis-aligned cell boxes
  of the flat layout (`view::pickBox`). On the surface the cells are warped, so picking
  either ray-casts against the *curved* cell quads, or inverts the warp per cell and casts
  in lattice space. Whichever is chosen, the invariant is the one ADR-0011 already set:
  **the ray follows what was drawn**, or a click selects a different cell than the one under
  the cursor. The move camera (M11) already reads the trace and not the topology, so the
  route a move takes is drawn on the surface for free once the cells are.
- **Labels and marks.** Coordinates, the selection ring, legal-move highlights and the last-
  move mark are drawn on the surface cells, not in the flat layout.
- **The seams.** The edges the geometry glued keep their portal hue (as `view::seams` already
  gives them), so the surface and the seam colour agree.

## M17.4 Higher dimensions

For D ≥ 3 the "shape" is not an embedding but the lattice itself. M17 reuses the derived
D≥3 grid (M13.4) and the `view::layout` the game already draws: the toggle extrudes the
board along its depth and grid axes rather than warping it. This is a smaller step than the
2-D surfaces and mostly already exists.

## M17.5 The control and the setting

- A `Settings::geometryView` (`flat` | `surface`) beside `flatView`, and a button and a key.
  The identity is `flat`, so the shipped look is a strict no-op and every existing golden
  still holds.
- The morph is a pure function of a progress value, like the overture, so it is
  `--shot`-able and reversible; the front end owns the clock.
- `standard` and every non-glued variant stay flat: there is no surface to become, and a
  setting that did nothing there would be a lie.

## Tests and acceptance

- **The surface is the one the overture draws:** the play board at `t = 1` for `cylinder`,
  `torus`, `mobius` and `klein` uses `derivedSurfaceAt`, and a test asserts the cell centres
  land exactly where the overture's do for the same `(u, v)`.
- **Picking agrees with drawing:** for every cell, project its centre with the *effective*
  camera and pick that pixel back; it selects the same cell. On a glued board too.
- **`flat` is a strict no-op:** with `geometryView = flat`, every existing render golden and
  the flat-layout picking tests are unchanged.
- **The position is not touched:** toggling the view changes nothing in `VariantId`, the
  hash, the move list, or the FEN; it is presentation, like `shape`/`height`.
- **`--shot`:** `chessbox_gui torus --shot t.ppm --geometry surface` renders the board as a
  torus, validation-clean, and `--clip` morphs it.

### Acceptance facts

1. A button turns the play board into its variant's shape and back, and a move is playable
   on the shape.
2. Picking on the surface selects the cell under the cursor, on a glued board as on a flat
   one.
3. `standard` is bit-identical; a glued variant's true colour/edge semantics still hold.
4. The morph is a pure function of `t`, so a still is reproducible and the reverse is free.

## Dependencies and ordering

1. **M17.1/M17.2** the surface and the cells (reuse M13) — the picture.
2. **M17.3** picking and marks — the part that makes it a *game* and not a diorama.
3. **M17.4** the D≥3 lattice.
4. **M17.5** the setting, the button and the key.
5. Tests land with each step.

## Risks and non-goals

- **Picking on a curved, possibly non-orientable surface is the whole risk.** A wrong
  inverse picks the wrong cell and the view becomes untrustworthy; the test above is the
  gate. If an exact inverse is out of reach, ray-cast the drawn quads, which cannot disagree
  with the drawing because it is the drawing.
- **Readability.** A warped board is harder to play on than a diagram, which is why the flat
  view stays the default and the surface is a deliberate mode, not a replacement.
- **Non-goals:** no change to movegen, geometry or the temporal model; the toggle is
  presentation and must not enter `VariantId`; no free-form reshaping of authored variants;
  the surface is derived from the identifications, never drawn by hand.
