# ChessBox Roadmap

High-level plan. Every milestone has its own detailed plan file in this
directory. Read `docs/ARCHITECTURE.md` first.

## Rules of engagement (apply to every milestone) **[INVARIANT]**

1. **Test-first, always.** No production line is written before a failing test
   names the behaviour it must produce.
2. **100% of tests pass before the next step begins.** Not the next milestone —
   the next *step*. A red suite blocks all forward motion.
3. **No step is done without: unit tests, a property/differential test where an
   invariant exists, a doc update, and a bench if it is on a hot path.**
4. **Every architectural decision gets an ADR** in `docs/adr/`, numbered,
   immutable once merged (supersede, never edit).
5. **The naive oracle is sacred.** Optimisations are proven equal to it.
6. **Determinism is a test, not an aspiration.** Every milestone adds to the
   replay corpus.
7. **AGENTS.md is updated in the same commit** as anything that changes how an
   agent should navigate or build the repo.

## Milestone sequence

| # | Milestone | Ships | Status | Detailed plan |
|---|---|---|---|---|
| M0 | Foundations & infrastructure | nix flake, CMake, CI, test harness, L0 base | **done** | [M0](M0-foundations.md) |
| M1 | 2D generalized core engine, headless | standard chess, perft-exact to depth 6, ASCII CLI | **done** | [M1](M1-core-2d.md) |
| M2 | N-dimensional generalization | 3D/4D boards, dimension-lift invariance | **done**, dim dispatch deliberately not built | [M2](M2-nd-generalization.md) |
| M3 | Boundary geometry | cylinder, torus, Möbius, Klein, mirrors, N-D analogues | **done**, two gaps recorded | [M3](M3-geometry.md) |
| M4 | Vulkan renderer + interaction | playable 2D/3D/4D+ board on screen | next | [M4](M4-renderer.md) |
| M5 | Variant VM + custom fields | explosive, checkers, regional variants; quantum design | | [M5](M5-variant-vm.md) |
| M6 | Temporal / multiverse (5D chess) | faithful 5D chess, generalized extra axes | | [M6](M6-temporal.md) |
| M7 | Client–server multiplayer | authoritative server, lockstep-verified replay | | [M7](M7-net.md) |
| M8 | Steam Workshop + packaging | variant packages, validation, signing, distribution | | [M8](M8-workshop.md) |
| M9 | Trainable per-variant AI | search + learned eval, self-play harness | | [M9](M9-ai.md) |

Each completed milestone's plan file ends with a **Status** section recording what was
built, what was deferred, and why - including the places where the plan turned out to
be wrong. Read those before trusting the plan text above them.

### Why this order

- **Headless before pixels (M1 before M4).** The engine's design will move a
  lot during M1–M3; renderer code written against an unsettled core is thrown
  away. Graphics is also the least testable part of the system, so it should be
  built against a core that is already proven.
- **Dimensions before topology (M2 before M3).** Boundary identifications act on
  coordinate *and* direction vectors; getting that right is far easier when
  arbitrary-dimension vectors already work and are tested.
- **Topology before the renderer (M3 before M4).** The renderer must visualise
  seams and wrapping. Designing the view layer before knowing what a Klein seam
  looks like in the data guarantees a rewrite.
- **Variant VM after geometry (M5).** The VM's effect primitives need to speak
  in terms of regions and neighbourhoods, which are geometry concepts.
- **Time travel late (M6).** It is the most rule-dense subsystem and it sits on
  top of everything else; it is also where an unproven core hurts most.
- **Escape hatch:** if visible progress is needed sooner than M4, M2 ends with a
  terminal (TUI) multi-slice viewer — cheap, testable, and enough to play 3D/4D
  boards by hand. The Vulkan work stays where it is.

## Milestone exit gates

A milestone is complete only when **all** of the following hold. These are
checked by `nix flake check`, which is the single source of truth. **[INVARIANT]**

- `ctest` green on gcc and clang, in Debug and Release.
- Green under ASan+UBSan; green under TSan once threads exist (M4+).
- `clang-tidy` clean at the configured level; no new warnings at `-Wall -Wextra
  -Wpedantic -Werror`.
- Line coverage of `src/core/` ≥ 90%, branch coverage ≥ 80%.
- The milestone's named acceptance facts (in its plan file) all hold, each backed
  by a named test.
- Benchmarks recorded to `bench/baselines/`; no unexplained regression >10%.
- No allocation-tripwire violations in movegen tests.
- Replay corpus reproduces byte-identical hashes.
- `docs/ARCHITECTURE.md`, `AGENTS.md`, and the milestone plan reflect reality.

## Cross-cutting risk register

| Risk | Milestone | Mitigation |
|---|---|---|
| Combinatorial blowup of direction sets in high dims | M2 | budget check at load; per-variant limits; atom order caps; tests asserting table sizes |
| Transport-table memory explosion | M3 | boundary-only tables + analytic fallback + budget config (ARCH §4.2) |
| Non-orientable geometry has genuinely ambiguous rules (what is "forward" for a pawn on a Klein bottle?) | M3 | make it an explicit, documented, per-variant policy choice; test each policy |
| Rule VM expressiveness insufficient for a target variant | M5 | primitive-extension process documented; each target variant is an acceptance test, so gaps surface early |
| 5D chess rules subtly wrong | M6 | golden test corpus transcribed from the reference game's known positions/puzzles before implementing |
| Vulkan complexity delaying everything | M4 | strict scope: one pipeline, instanced quads/cubes, no PBR, no shadows in M4 |
| Determinism drift (float, iteration order, hash) | all | no floats in engine core **[INVARIANT]**; canonical iteration orders; replay corpus in CI |
| Quantum chess may not fit the model | M5 | scoped as a design study with an interface hook, not promised as delivered |

## Definition of "generalizable" used throughout

A feature is generalizable if adding the next member of its family is *data or
configuration*, not new engine code. Concretely, the acceptance question for each
milestone is: "what does it cost to add the (n+1)-th case?" Answers are recorded
in each plan file's **Generalization test** section.
