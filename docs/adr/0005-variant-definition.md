# ADR-0005: Variants as declarative data plus a sandboxed rule-effect VM

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
Variants must come from users via Steam Workshop, must work identically on every
client in a multiplayer game, must be safe to load from strangers, and must be
expressive enough for explosive chess, checkers, and regional variants — including
their custom per-piece data fields.

## Decision
A variant is pure data: dimensions, geometry identifications, piece vector-move
atoms, custom field schemas, and rules expressed as an ordered table of
`(trigger, condition, effect[])` entries interpreted by a compiled-in effect VM
with typed opcodes, no backward jumps, and a hard step budget. New effect
primitives are added in C++ as the variant catalogue demands them; that escape
hatch is expected and cheap.

## Consequences
Safe by construction for Workshop content — there is no code to sandbox, and
termination is structural. Deterministic across machines and compilers, which
multiplayer and self-play both require. Serializable, so a rule set hashes into
the variant identity. The cost: expressiveness is bounded by the primitive
catalogue, so genuinely novel mechanics need an engine change (roughly 100 lines
plus tests, then reusable everywhere). Superposition (quantum chess) does *not*
fit this model and is scoped separately (M5.5).

## Alternatives considered
- **Embedded Lua:** immediately expressive, but sandboxing, cross-machine
  determinism, and hot-loop performance all become serious problems.
- **WASM rule modules:** sandboxed, expressive and deterministic — the strongest
  long-term answer, and the natural successor if the primitive catalogue proves
  insufficient. Rejected for now on dependency weight and authoring UX.
- **Native C++ plugins:** unacceptable for untrusted content.

## How to reverse this
Adding WASM later is additive: rules become one more rule *backend* behind the
same trigger/effect interface.
