# ADR-0019: A square on the play surface is a patch of it, and the board is posed

- **Status:** Accepted
- **Date:** 2026-09-30

## Context

M17 turned the play board into the surface its geometry describes. The first cut placed
each cell at its point on that surface and turned the cell's local +Z onto the surface
normal - and stopped there. Four things followed, all visible in one screenshot:

- **A normal is not a frame.** The shortest arc onto a normal leaves the tile free to spin
  in its own plane, so the squares came out at arbitrary angles to each other. A board of
  squares at arbitrary angles is not a board.
- **A surface has a metric.** The tiles were sized as though the board were still flat,
  but an embedding stretches the board where it opens a hole and squeezes it where it
  closes one - a torus's hole is pulled open by 2.2 - so the squares covered under half of
  their cells in places and sat on top of each other in others.
- **A square is not a rectangle.** Even sized and turned correctly, a flat tile is tangent
  to the surface at exactly one point. Where the surface curves fast - a Klein bottle's
  figure-eight turns most of a right angle from one square to the next - neighbouring
  tiles cut into each other at one end and stand off it at the other, and the board reads
  as fish scales with hard edges rather than as a surface. Subdividing a tile into smaller
  tiles makes the scales smaller; it does not stop them being scales.
- **A surface is not rigid, and a board on a surface can move along it.** A player looking
  at a torus from outside can never see the inside of the hole, and - more to the point -
  cannot feel that the a-file and the h-file are the same edge by looking at a picture of
  them touching.

The first three are bugs. The fourth is a *decision*: the shape a variant's
identifications imply is fixed, but neither the embedding of that shape nor where the
board sits on it is. A torus with its ring radius swept through zero to its own negative
is the same torus, turned inside out; a board drawn one cell further along the surface is
the same board, and watching a1 ride round to where h1 was is the gluing itself rather
than a diagram of it.

## Decision

- **A square is a patch of the surface, not a shape placed on it.** Each cell is built as
  a grid of corners lying on the surface at its own lattice footprint, with a little
  thickness, and the whole board goes to the card as **one mesh** rebuilt each frame, with
  its colours in its vertices (`MeshVertex::color`, white on every authored shape). No
  orientation to get wrong, no size to get wrong, and two squares either side of a lattice
  edge are cut from the same curve, so they cannot cross. The pieces stay instanced: a
  piece *is* an object standing on the surface, and an object does have an orientation.
- **A play surface is a shape plus a pose.** `SurfacePose` carries the pose: `slideU` and
  `slideV`, where the board sits *on* the surface in cells; `evert`, how far the surface
  has been turned through itself; and `stretch`, how much of the Moebius ribbon's
  legibility stretch the board keeps. `derivedSurfaceAt(kind, u, v, pose)` is the surface;
  the three-argument form is `SurfacePose{}` and is bit-identical to what the library
  screen draws, so every existing overture and capture is untouched.
- **The slide is sampling, not a special case.** Every warp is a smooth function of `u`
  and `v` over the whole real line and already satisfies the variant's gluing there, so
  sliding the board one cell along the files is drawing it at `u + 1/nx`. That is why a1
  lands *exactly* where b1 was, why a lap brings the board home, and why a lap of a
  Moebius band brings it home mirrored and needs a second - the gluing is not simulated,
  it is the surface.
- **A pose is a pure function of its numbers**, exactly as an overture is a pure function
  of `t`. Nothing is integrated, nothing is remembered between frames. Dragging back
  retraces the drag, and `--slide`, `--slide-v` and `--evert` reproduce a still.
- **A pose is presentation.** It does not enter `VariantId`, the hash, the move list or
  the FEN. The cells, their identifications and every legal move are the same at any
  value; what changes is where the cells are drawn and which way is out.
- **Each surface everts in its own way, and the way is the surface's own.** The closed
  ones sweep their ring radius through zero to its negative, which carries the cross
  section round the axis and leaves the inner equator outside - by way of the spindle,
  where the hole has closed to a point. A cylinder cannot: it is open at both ends, and no
  rotation turns it inside out, so it is rolled back over itself the way a sock is.
- **`PlaySurface` owns the whole placement** - the patch each square is, the seat a piece
  stands on, and the ray a click is tested against. One sampling answers all of them, so
  ADR-0011's invariant - the pick follows what was drawn - holds by construction rather
  than by care.
- **The instance's scale is applied in the mesh's own frame, before its orientation.**
  Scaling after a rotation scales the world axes rather than the mesh's; the two orders
  agree exactly wherever the orientation is identity, which is the whole of the flat
  board, so this is a no-op everywhere except on the pieces standing on the surface.
- **The middle mouse button slides the board round its surface while the geometry view is
  on.** The view frames and centres the shape the moment it is switched on, so there is
  nothing left for a pan to do, and riding a1 round to b1, to c1 and eventually back to a1
  is the only way to *feel* a gluing rather than be told about it. Sliding along the ranks
  is offered only where the ranks are glued. `[` and `]` step the eversion for a keyboard,
  and the three capture flags state any pose.

## Consequences

The geometry view draws a board rather than a scatter of tiles, and the two properties of
a glued surface that a camera cannot show - what is round the back of the gluing, and what
is inside the hole - both have controls. A variant that glues two axes gets all of it from
its identifications, with nothing authored.

Costs. The surface and its mesh are rebuilt each frame and each click - a few thousand
evaluations of a warp and about twenty thousand vertices for an 8x8 board, which is
microseconds and a buffer upload. It is not cached, deliberately: a cache keyed on the
pose is the thing that would let the picture and the pick ray disagree. The board is no
longer one instanced draw but one mesh draw, so a very large glued board uploads geometry
proportional to its cell count; the instanced path still carries every other screen and
every board above two dimensions. Eversion passes through genuinely degenerate
embeddings - the spindle torus is self-intersecting, and a cylinder's fold has the surface
doubled back against itself - which are drawn honestly rather than hidden, because the
degeneracy is the moment a player is watching for.

## Alternatives considered

- **Keep the squares as instanced slabs and subdivide harder.** Tried, and it is where the
  fish scales came from: subdivision makes the scales smaller without making them patches,
  and every tile still has to be given an orientation and a size that can only be right at
  its own centre.
- **One draw call per square, so the colour can stay on the instance.** Sixty-four draws
  for an 8x8 board rather than one, and the number grows with the board. A colour on the
  vertex costs four floats and keeps the board one draw.
- **Animate the eversion on a clock rather than binding it to a drag.** It would need
  state outside the surface, and the whole reason an overture is pure in `t` is that state
  is what makes a capture irreproducible. A drag is the same function with the player
  holding the number.
- **A second, hand-authored "turned" surface per topology, blended into.** Twice the
  geometry to keep in step, and the blend would pass through shapes that are not the
  variant's surface at all. Sweeping a parameter of the one embedding cannot leave it.
- **Let the camera go inside the shape instead.** It shows the inside but not the turn,
  and picking a cell through the far wall of a torus is worse, not better.

## How to reverse this

`SurfacePose{}` is the identity, so dropping any pose field is deleting it and its two
call sites; `PlaySurface` stands without them. The vertex colour is white everywhere else,
so the shader's multiply is a no-op off this path, and the shader's scale order only
matters for instances that carry an orientation - which, outside the geometry view, none
do.
