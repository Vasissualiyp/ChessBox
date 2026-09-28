# M5 — Rule-Effect VM & Custom Data Fields

**Goal:** the long tail of variants becomes data. Explosive chess, checkers, and
regional variants ship as TOML. Arbitrary per-piece and per-cell fields exist,
are hashed, are serialized, and cost nothing when unused.

**Exit condition:** the target variants below play correctly with zero
variant-specific C++ in `src/`.

## M5.1 Custom field system

```toml
[[piece.field]]  name = "charge"  type = "i8"  default = 0  hashed = true
[[cell.field]]   name = "scorched" type = "bool" default = false hashed = true
```

- SoA columns sized to the declared maxima; zero columns when none declared.
- `hashed` fields participate in Zobrist; unhashed ones are cosmetic. Test:
  changing a hashed field changes the hash, an unhashed one does not.
- `make`/`unmake` restores every column exactly (extends the M1 property test).
- Serialization: FEN-N gains a field section; round-trip property tested.
- **Tests:** a variant with 16 fields costs no measurable movegen time versus one
  with none (bench gate), proving the "free when unused" claim.

## M5.2 VM core

- Typed value model: ints, cells, dirs, pieces, colours, bools, small sets.
  No floats. **[INVARIANT]**
- Flat rule table `(trigger, condition, effect[])`, ordered; execution is a small
  stack machine with a hard step budget and no backward jumps — termination is
  structural, so hostile Workshop content cannot hang the game. **[INVARIANT]**
- Determinism: fixed evaluation order, no iteration over hash containers, no
  address-dependent behaviour. Tested by running a corpus on both compilers and
  both optimisation levels and comparing hashes bit-for-bit.
- Validation: every opcode has a typed signature checked at load, with a
  user-facing error naming the rule and the offending argument.
- Tracing: `diag` can dump a rule-execution trace, which is what makes authored
  variants debuggable at all.

## M5.3 Trigger and effect catalogue (v1)

Triggers: `OnMoveGenerate`, `OnMoveValidate`, `OnMoveStart`, `OnCapture`,
`OnMoveEnd`, `OnEnterRegion`, `OnTurnStart`, `OnTurnEnd`, `OnGameResultQuery`.

Effects: `Destroy`, `MovePiece`, `Spawn`, `Transform`, `SetField`, `AddField`,
`DestroyRegion`, `ForbidMove`, `GrantExtraMove`, `SetTurnOrder`, `EndGame`,
`Promote`.

Each effect: opcode, signature, validator rule, interpreter case, step cost, unit
tests in isolation, and at least one variant using it as an acceptance test. The
`cb-new-effect` skill encodes this checklist.

## M5.4 Target variants (acceptance set)

| Variant | Exercises | Notes |
|---|---|---|
| Atomic/explosive chess | `OnCapture` + `DestroyRegion` | region defined via geometry, so it is correct on a torus too — tested |
| Checkers / draughts | `Must`-capture atoms, `GrantExtraMove` multi-jump, promotion region, non-royal win condition | the sharpest test of the VM; also the first variant with a forced-capture move filter |
| Makruk / Shatranj / Xiangqi-subset | different atom sets, `Hop` mode, region-restricted pieces (palace/river) | exercises cell-region predicates |
| Atomic on a Klein bottle | composition of M3 and M5 | proves the layers are genuinely orthogonal |
| Racing-kings-style | `OnEnterRegion` + `EndGame` | non-checkmate win conditions |
| Duck chess / obstacle pieces | `Spawn`, neutral pieces, `ForbidMove` | third-party (colourless) pieces |

The last row of the table is the real acceptance criterion: **composition**. Any
variant feature must work on any geometry in any dimension, and the test matrix
asserts a sample of the cross-product.

## M5.5 Quantum chess — design study, not a delivery

Superposition cannot be expressed by fields, and this plan does not pretend
otherwise. The design study produces:

- An `IPosition` interface narrow enough that an `EnsemblePosition` (a list of
  basis positions with integer-ratio amplitudes, measurement on capture,
  interference on repeated paths) can be added without touching L4/L5.
- A written analysis of the cost (movegen over an ensemble is O(basis states)),
  the determinism story (fixed-seed measurement, integer arithmetic, no floats),
  and the UI implications.
- An ADR stating whether to build it, deferred to after M6.

**It is explicitly out of scope for M5 delivery.** Saying so now is cheaper than
discovering it at the end.

## Generalization test

A new variant in the target family is a TOML file. A genuinely new *mechanic*
costs one effect primitive (~100 lines + tests) and is then reusable by everything.

## Acceptance facts

1. Every variant in M5.4 plays correctly, with goldens, and zero C++ added to
   `src/` for any of them.
2. A 16-field variant costs no measurable movegen time versus a 0-field variant.
3. Rule execution is bit-identical across gcc/clang and Debug/Release over the
   corpus.
4. A malformed or hostile rule set is rejected at load or bounded at runtime;
   fuzz-tested to ≥ 1e6 cases with no hang, crash, or unbounded memory.
5. Atomic-on-Klein and checkers-on-a-torus both work, proving orthogonality.
6. The quantum ADR is written and merged.
