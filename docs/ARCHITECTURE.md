# ChessBox Architecture

> Status: design, pre-implementation. Authoritative for module boundaries and
> invariants. Changes to anything marked **[INVARIANT]** require a new ADR in
> `docs/adr/`.

## 0. One-paragraph summary

ChessBox is a FOSS sandbox for *finite-board* game variants of essentially
arbitrary shape: any number of spatial dimensions, arbitrary boundary topology
(cylinder, torus, Möbius, Klein bottle, real projective plane, and their
higher-dimensional analogues), arbitrary piece movement expressed in a single
**vector-move algebra**, arbitrary per-piece/per-cell **custom data fields**,
and extra *temporal* and *multiverse* axes reproducing 5D-chess-style time
travel. The engine core is dependency-free C++23, driven by declarative variant
data and a sandboxed rule-effect VM. A Vulkan renderer projects N-D boards to
the screen. Performance is a first-class constraint at every layer.

## 1. Layer map

Strictly downward dependencies. A layer may only include headers from lower
layers. Enforced by `tests/arch/test_layering.cpp` (include-graph scan) and by
per-directory CMake link visibility. **[INVARIANT]**

```
L10  frontends      cli/  render/ (Vulkan)  net/  app/
 L9  io             variant TOML loader, notation, FEN-N, replay, Workshop pkg
 L8  game           Game, history, undo, adjudication, repetition, clocks
 L7  temporal       turn/timeline axes, present, branching, cross-board attacks
 L6  rules          effect VM: triggers, conditions, effects, win conditions
 L5  movegen        expansion, ray walk, staged gen, legality, perft
 L4  pieces         vector-move algebra, PieceType tables, direction sets
 L3  position       cells, piece lists, occupancy bitsets, field columns, hash
 L2  geometry       boundary identifications, transition group, transport
 L1  space          DimSpec, Coord, Direction, strides, flat CellId
 L0  base           fixed-capacity containers, arenas, expected, log, RNG
```

Two extra vertical modules cut across, by design, with no upward deps:

- `variant/` (L4.5): the immutable, fully-resolved `VariantSpec` — everything
  L5..L8 needs, precomputed once at load. Nothing below L9 ever parses text.
- `diag/` (L0.5): deterministic tracing/counters, compiled out in release.

## 2. The three representations of "a square"

This distinction is the single most important performance idea in the project.
**[INVARIANT]**

| Type | Size | Used by | Purpose |
|---|---|---|---|
| `Coord` | 18 B POD | L9 IO, L10 UI, tests | human/authoring coordinates, `(x,y,z,...)` |
| `CellId` | 4 B (`uint32`) | L3..L8, all hot paths | flat index into the lattice; the *only* thing movegen touches |
| `DirId` | 2 B (`uint16`) | L4..L7 | index into the variant's global direction table |

Conversion `Coord <-> CellId` happens only at the IO/UI boundary. Movegen never
decodes a coordinate; it adds strides and consults the geometry only when a step
leaves the interior (§4.2).

```cpp
constexpr int kMaxDims = 8;                 // compile-time budget, ADR-0003
struct Coord {                              // trivially copyable, no heap
  int16_t c[kMaxDims];
  uint8_t n;                                // active dims
};
using CellId = uint32_t;                    // kInvalidCell = 0xFFFF'FFFF
using DirId  = uint16_t;
```

`kMaxDims = 8` covers the spec's worst named case (5D chess on a 5-torus = 5
spatial + turn + timeline = 7). Raising it is a one-line change plus a rebuild;
nothing hardcodes 8 outside `dims.hpp`.

## 3. L1 space

`DimSpec` is the shape of the lattice, built once per variant:

```cpp
enum class AxisKind : uint8_t { Spatial, Temporal, Multiverse };
struct Axis { int16_t extent; AxisKind kind; char name[7]; };
struct DimSpec {
  Axis    axis[kMaxDims];
  uint8_t n;
  uint32_t stride[kMaxDims];   // row-major; stride[0] = 1
  uint32_t cellCount;          // product of extents
};
```

`AxisKind` is metadata only at L1–L5: movegen treats every axis identically.
Only L7 gives `Temporal`/`Multiverse` axes their special semantics. This is what
makes "toroidal timelines" and "two multiverse dimensions" configuration rather
than a rewrite (ADR-0007). **[INVARIANT]**

A `Direction` is a signed integer vector, stored expanded in the variant's
direction table together with its precomputed flat stride delta and its
per-axis interior margins:

```cpp
struct DirEntry {
  int16_t  v[kMaxDims];     // the vector, e.g. (1,2,0,...)
  int32_t  delta;           // sum v[i]*stride[i]  -- interior fast path
  int16_t  loMargin[kMaxDims], hiMargin[kMaxDims];  // interior test bounds
};
```

## 4. L2 geometry

### 4.1 Model

Topology is declared as a set of **boundary identifications**. Each identifies
one face of the box with another, carrying an affine coordinate transform and
the induced action on direction vectors (ADR-0004):

```
identify x=W  ==  x=0                                   # cylinder / periodic x
identify y=H  ==  y=0  with  x -> W-1-x, dir.x -> -dir.x # Klein seam
identify x=W  ==  x=W-1 with dir.x -> -dir.x             # mirror / reflecting
```

The set generates a finite **transition group** of transforms
`T = (permutation, sign flips, offsets)`. Crossing a seam applies `T` to *both*
the position and the moving direction — so a rook crossing a Klein seam comes
out travelling along a different (correctly reoriented) line, and a bishop's
diagonal survives an axis swap. Transporting the direction, not just the
position, is what makes non-orientable boards actually correct rather than
merely non-crashing. **[INVARIANT]**

### 4.2 The hot path: interior stride + boundary transport

```cpp
// Step one unit of direction d from cell c. O(1), branch-predictable.
inline StepResult step(CellId c, DirId d) const {
  if (interior_.test(c, d))                  // precomputed margins, one test
      return { CellId(c + dir_[d].delta), d };// pure integer add: no wrap math
  return transport_.lookup(c, d);            // boundary: table or analytic
}
```

- Interior cells (the overwhelming majority for any interesting board) cost one
  add. No modulo, no coordinate decode, no branch misprediction in the common
  case.
- Boundary cells consult a transport table keyed by `(cell, dir)`, built lazily
  and only for boundary cells — memory is `O(boundaryCells × dirs)`, not
  `O(cells × dirs)`. For a 7-D board this is the difference between megabytes
  and gigabytes.
- If a variant's boundary table would exceed a configured budget, the loader
  falls back to the analytic transform path (slower, always correct). Both paths
  are verified against each other by a differential test. **[INVARIANT]**

Open (non-identified) faces produce `kInvalidCell`, which terminates a ray.

### 4.3 Degenerate geometry

Identifications can make a cell its own neighbour, or make two coordinates
denote the same cell (projective identifications). The loader canonicalises
cells into equivalence classes and validates that the resulting quotient is a
consistent lattice; failures are user-facing validation errors, never UB.

## 5. L3 position

```
cells      : flat array[cellCount] of Piece (packed uint32: type, color, flags)
pieceList  : per-color vector<PieceRef>      -- iteration order is canonical
occupancy  : bitset words; 8x8 single-word fast path
fields     : SoA columns, one per declared custom field (ADR-0005)
state      : sideToMove, halfmove/fullmove, ep target, castling rights, ...
hash       : incremental Zobrist over cells + state + every hashed field column
```

Design notes:

- Cells are a flat array, never nested vectors. One allocation per position.
- Occupancy bitsets exist so that "is the path clear" and "who attacks X" can be
  answered wordwise. For the 2D 8×8 case this degenerates to classical
  bitboards, giving standard-chess-competitive speed as a *special case* of the
  general engine, not a separate code path.
- Custom fields are columns, not per-piece maps. A variant declaring
  `piece.field.charge: i8` gets one `int8_t[maxPieces]` column. Zero cost when
  unused. **[INVARIANT]**
- `make/unmake` is the only mutation API and is exactly reversible, including
  hash and all field columns. A property test asserts this over random
  playouts. **[INVARIANT]**

## 6. L4 the vector-move algebra

The spec's notation is formalised as a **MoveAtom**:

```
atom := magnitudes (m1..mr), r <= D, all mi != 0   # "NULL" = pad with zeros
      × expansion  (axis assignment × sign choice, deduplicated)
      × maxK       (1 = leaper, >1 = limited rider, inf = rider/slider)
      × mode       (Slide | Leap | Hop)
      × capture    (May | Must | Cannot)
      × flags      (firstMoveOnly, forwardOnly, promotes, pathMustBeEmpty, ...)
```

Expansion of `(m1..mr)` in `D` dims enumerates every injective placement of the
magnitudes onto distinct axes and every sign combination, deduplicated. So:

| piece | atoms | 2D directions | 3D directions |
|---|---|---|---|
| rook | `[1]^inf` slide | 4 | 6 |
| bishop | `[1,1]^inf` slide | 4 | 12 |
| knight | `[1,2]^1` leap | 8 | 24 |
| king | `[1]^1 + [1,1]^1` | 8 | 18 (+8 with `[1,1,1]^1`) |
| nightrider | `[1,2]^inf` slide | 8 | 24 |

Whether a piece also gets higher-order atoms in higher dimensions (`[1,1,1]`
for a 3-D queen) is **declared per variant**, never inferred — the interesting
variants disagree about this, so it must be authorable.

Pawns, castling and other irregulars are expressed as:

- **Oriented atoms**: a designated orientation axis and per-colour sign give
  "forward" a meaning in any dimension.
- **CompoundMove templates**: an ordered list of displacements plus conditions
  (`unmoved`, `pathEmpty`, `notAttacked`), which covers castling in any
  dimension, and any future multi-piece move.
- **Rule hooks**: en passant = an ep-target board field plus a trigger/effect
  pair in the L6 VM. Promotion = a trigger on reaching a declared region.

Expansion runs once at variant load, producing the global direction table and
per-`PieceType` `DirId` spans. Movegen then iterates a contiguous span of
`DirId`s — no combinatorics at runtime. **[INVARIANT]**

## 7. L5 movegen

```
for each piece p of sideToMove:                      // canonical order
  for each atom a of p.type:
    for each DirId d in a.dirSpan:
      walk: c = p.cell
            for k in 1..a.maxK:
              (c,d) = geometry.step(c,d)              // §4.2
              if c == invalid: break
              classify: empty / own / enemy / blocked-by-rule
              emit or break per a.mode and a.capture
```

- **Staged generation**: captures, then quiets, then compound/special. Callers
  (search, later) can stop early.
- **Legality**: `make` + "is the royal attacked" by default; an incremental
  attack-map path is an optimisation added later behind a differential test,
  never as the first implementation.
- **Reference oracle**: a deliberately naive, obviously-correct generator
  (decode coordinates, brute-force every cell, analytic geometry) lives in
  `tests/oracle/`. Every optimisation is validated against it by differential
  testing over randomly generated variants. This oracle is the backbone of the
  TDD strategy and is never allowed to rot. **[INVARIANT]**
- **Perft** is a first-class engine feature, not a test helper: standard chess
  must reproduce the published node counts to depth 6 exactly (§ M1).

## 8. L6 rule-effect VM

Declarative, sandboxed, deterministic (ADR-0005). Not a scripting language.

```
trigger  := OnMoveStart | OnMoveEnd | OnCapture | OnEnterRegion | OnTurnEnd
          | OnPromotionRank | OnGameStateQuery ...
condition:= typed expression over (position, move, fields, constants)
effect   := Destroy | Move | Spawn | Transform | SetField | AddField
          | DestroyRadius | ForbidMove | GrantMove | EndGame ...
```

Rules are a flat, ordered table of `(trigger, condition, effect[])`, evaluated
on a small typed value stack with a hard step budget. Consequences:

- Safe to load arbitrary Workshop content: no code execution, no I/O, no
  unbounded loops.
- Deterministic across machines and compilers — a hard requirement for
  multiplayer replay and for the later AI's self-play.
- Serializable, so a rule set hashes into the variant identity.

Explosive chess = `OnCapture -> DestroyRadius(r=1)`. Checkers = `Must` capture
atoms + a promotion trigger + a compound multi-jump. Indian/regional variants =
different atom sets and promotion regions. New primitives are added in C++ when
a variant genuinely needs one; the escape hatch is expected and cheap.

**Quantum chess is explicitly a research item** and is *not* claimed as
achievable by fields alone: it needs a position *ensemble* (a superposition of
basis positions with amplitudes, with measurement on capture). The design hook
is that L8 talks to L3 through a narrow interface, so an `EnsemblePosition` can
be introduced later without touching movegen. Sketched in `docs/plan/M5`.

## 9. L7 temporal / multiverse

5D-chess semantics implemented faithfully, but as *policies over extra axes*
(ADR-0007):

```
Coord = (spatial..., t, l)         # t = turn axis, l = timeline axis
PresentPolicy  : which boards are "in the present" and must be moved
BranchPolicy   : when a move onto a past board creates a new timeline
TurnPolicy     : how many boards move per submitted turn; submission/commit
ArrivalPolicy  : what a piece does on arriving from another board
```

Movegen is unchanged: a "jump through time" is simply a direction with a nonzero
component on the `t` or `l` axis, and the geometry layer handles the rest. This
is why a toroidal timeline is a boundary identification on `l`, not a feature.
Cross-board check is ordinary attack generation over the full lattice.

## 10. L10 renderer (Vulkan)

- SDL3 window/input; Vulkan 1.3 core, dynamic rendering, `VK_KHR_synchronization2`;
  VMA for allocation; shaderc/glslang offline via CMake to SPIR-V.
- One instanced draw per (piece type × cell state) class. A cell is an instance,
  not a draw call. Millions of cells must be a buffer upload, not a scene graph.
- **N-D projection** is a pipeline of declarative view transforms: choose 2 or 3
  "screen axes", render the remaining axes as a laid-out grid of sub-boards
  (5D-chess style), with configurable spacing, and per-axis fold/unfold
  animation. The projection config is data, so new view styles are data too.
- Geometry seams are visualised (ghosted wrap previews, seam highlighting) —
  non-orientable boards are unplayable without this.
- Renderer reads an immutable `PositionView` snapshot; the engine never blocks
  on the GPU and the renderer never mutates game state. **[INVARIANT]**

## 11. Determinism, hashing, persistence

- Zobrist over cells, state, and hashed field columns; keys derived from a fixed
  seed and the variant hash, so hashes are reproducible across runs and hosts.
- `VariantId = hash(canonical serialization of VariantSpec)`. Multiplayer and
  replays compare `VariantId`, never file names.
- Replay format: variant id + seed + move list; replaying must reproduce every
  intermediate hash. A CI test replays a corpus. **[INVARIANT]**

## 12. Performance discipline

- `bench/` targets with committed baselines; CI fails on >10% regression in
  perft NPS or movegen throughput.
- No allocation in movegen. Fixed-capacity `SmallVec` + per-search arena;
  an allocation-tripwire allocator asserts this in debug tests. **[INVARIANT]**
- Hot kernels are templated on dimension count and dispatched through a small
  table (`dispatch<2..8>`), so loops over axes are fully unrolled; the dynamic
  path exists for `n > kMaxDims` correctness only.
- Profiles committed under `bench/baselines/` per-architecture, with the
  measurement recipe, so numbers are comparable over time.
