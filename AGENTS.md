# AGENTS.md — ChessBox

Read this first. It is the map. Design rationale is in
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md); the plan is in
[`docs/plan/00-roadmap.md`](docs/plan/00-roadmap.md); decisions are in
[`docs/adr/`](docs/adr/README.md).

**Status: pre-implementation.** Only planning documents exist. M0 is next.

## What this project is

A FOSS sandbox for finite-board game variants of arbitrary shape: any number of
dimensions, arbitrary boundary topology (torus, Klein bottle, …), piece movement
expressed in one vector-move algebra, custom data fields, and 5D-chess-style time
travel. Engine core is dependency-free C++23 driven by declarative variant data.
Performance and extensibility are the two hard constraints.

## Commands

```bash
nix develop                      # core dev shell (no graphics deps)
nix develop .#gfx                # adds SDL3 + Vulkan (M4+)
cmake --preset dev && cmake --build build/dev
ctest --preset dev               # all tests
ctest --preset dev -L unit       # fast loop (labels: unit property golden perft arch)
tools/precommit.sh               # format + tidy + fast tests — run before every commit
nix flake check                  # THE gate: both compilers, sanitizers, coverage, goldens, bench
```

## The rules (non-negotiable)

1. **Test first.** No production line before a failing test names the behaviour.
2. **Green before next step.** A red suite blocks all forward motion — not just
   the next milestone, the next step.
3. **Never "update" a golden to make a build pass.** Investigate.
4. **The naive oracle in `tests/oracle/` is sacred.** Every optimisation is proven
   equal to it by differential test. See ADR-0009.
5. **No floats in `chessbox_base` or `chessbox_core`.** Determinism. Enforced by a test.
6. **No allocation in movegen.** Enforced by the allocation tripwire.
7. **Architectural decisions get an ADR.** Immutable once merged; supersede, never edit.
8. **Update this file in the same commit** as anything that changes navigation or commands.

## Layer map — dependencies point strictly down (ARCH §1)

Each layer is a CMake target linking only to lower layers, so a violation is a
**link error**, not a review comment.

| Layer | Dir | Holds |
|---|---|---|
| L10 | `src/cli` `src/render` `src/net` `src/app` | frontends |
| L9 | `src/io` | variant TOML loader, notation, FEN-N, replay, packaging |
| L8 | `src/game` | history, undo, adjudication, repetition, clocks |
| L7 | `src/temporal` | turn/timeline axes, present, branching |
| L6 | `src/rules` | effect VM: triggers, conditions, effects |
| L5 | `src/movegen` | expansion, ray walk, staged gen, legality, perft |
| L4 | `src/position` | cells, occupancy, field columns, hash, make/unmake |
| L3 | `src/pieces`, `src/variant` | vector-move algebra, resolved `VariantSpec` |
| L2 | `src/geometry` | identifications, transition group, transport |
| L1 | `src/space` | `DimSpec`, `Coord`, `Direction`, strides, `CellId` |
| L0 | `src/base`, `src/diag` | containers, arenas, `Result`, bitsets, RNG, Zobrist, tracing |

## Three representations of "a square" — get this right (ARCH §2)

- `Coord` (18 B POD, `(x,y,z,…)`) — **only** L9 IO and L10 UI and tests.
- `CellId` (`uint32`, flat lattice index) — **everything hot**. Movegen never
  decodes a coordinate.
- `DirId` (`uint16`, index into the variant's global direction table).

## Where to add what

| Task | Go to | Skill |
|---|---|---|
| New piece | `variants/*.toml` (data only) | `cb-new-piece` |
| New variant | `variants/`, goldens, `docs/variants/` | `cb-new-variant` |
| New topology | `variants/*.toml` geometry block | `cb-new-geometry` |
| New rule mechanic | `src/rules/` opcode + tests + docs table | `cb-new-effect` |
| New module in a layer | that layer's dir + CMake edge + test + this table | `cb-new-module` |
| A decision | `docs/adr/` | `cb-adr` |
| Perft mismatch | bisect with `perft-divide` against the oracle | `cb-perft-golden` |

Skills live in `.claude/skills/` and are catalogued in
[`docs/plan/skills.md`](docs/plan/skills.md). Prefer them: the procedures are
long, multi-directory, and identical every time.

## Conventions

- C++23. `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`.
- Types `PascalCase`, functions `camelCase`, members `trailing_`, constants `kName`.
- SPDX header on every file: `// SPDX-License-Identifier: GPL-3.0-or-later`.
- Errors: `Result<T, ErrorCode>` in the engine; exceptions only at the IO boundary.
- Tests mirror the source tree: `src/movegen/ray.cpp` → `tests/unit/movegen/test_ray.cpp`.
- Commits: `<area>: <imperative summary>`; a golden change must explain itself.

## Gotchas

- Direction counts explode with dimension (`[1,2,3]` at D=8 → 2688 directions).
  There is a load-time budget; respect it.
- An atom of order `r > D` expands to *nothing* — deliberate, tested, not an error.
- On non-orientable boards, direction vectors must be **transported** through
  seams, not just positions. This is the #1 source of silent wrongness.
- "Forward", "last rank", and bishop colour-binding are meaningless on some
  topologies. They are per-variant policies; never hardcode them.
- `kMaxDims = 8` lives in `src/space/dims.hpp` and nowhere else.
