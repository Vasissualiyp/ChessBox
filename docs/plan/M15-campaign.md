# M15 — Campaign: The Geometry Ladder

**Goal.** Give the sandbox a *shape* and an ending: a designed progression from the board
a player knows to the board they cannot picture, with a boss at the end of each leg,
finishing on the six-dimensional torus. It is the narrative spine of M14's on-ramp and the
answer to the review that says "beautiful, but why am I here?".

**Exit condition.** A new player starts at the campaign, clears each leg — learning
exactly one geometry or rule mechanic per leg — and reaches the final `t6` boss. Every
boss is **fair and winnable**, tests a specific concept rather than raw search strength,
and is validated by the live engine so a rule change cannot silently break it. The whole
campaign runs deterministically and every screen is `--shot`-able.

---

## The one idea: a boss is an objective, not a strength check **[INVARIANT]**

The naive campaign makes the final boss "beat the engine on a 6-D torus". That is the one
thing this project cannot promise: M10 explicitly commits to *a legal opponent, not a
strength number*, and `t6` is the AI's worst case (the branching blowup M10 flags as
favouring MCTS). A climax built on a strength test is built on the milestone's weakest
guarantee, and chess bosses are notoriously unfair anyway — the player loses to search, not
to the board.

So a boss is a **scoped objective on a named board**, not an Elo wall:

- it names a **concept** (wrapping, mirroring, branching, a rule effect);
- it names a **win condition** in the variant's own terms;
- the opponent, when there is one, plays under a **declared handicap or constraint**;
- the final boss is a **puzzle position**, not an open game.

This makes every boss tunable, testable, fair, and — not incidentally — **clip-able**,
which is the same asset the release sequence is chasing.

---

## M15.1 The ladder — one concept per leg

Each leg is a small run of games that teaches one idea, then a boss that *requires* it.
The ladder is M14's curated path made into a progression, not a new system:

| Leg | Board | Concept the leg teaches | Boss objective (example) |
|---|---|---|---|
| 1 | `standard` | the interface, check/checkmate | win a normal game vs. the derived AI |
| 2 | `cylinder` | a move can leave one edge and arrive at the other | win using a wrapping move |
| 3 | `torus` | two gluings; "forward" is a choice | mate where the escape squares wrap |
| 4 | `mobius` / `klein` | a seam can *reverse* your piece | force mate with a returning bishop |
| 5 | `cube5` / `hyper4` | a third and fourth spatial axis | deliver mate across a depth slice |
| 6 | `5d` | branching timelines | win before the timeline forks |
| ★ | `t6` | all of it at once | **the finale** (M15.3) |

Leg content is data (a position + an objective + an opponent), validated against the
engine. Nothing here is a new engine feature — it is the existing variants, played to a
stated goal.

## M15.2 Boss design — objective, handicap, constraints

A boss is a `Scenario`:

```cpp
namespace cb::app {

enum class ObjectiveKind : std::uint8_t {
  Win,            // ordinary checkmate (leg 1)
  WinWithMove,    // win, and the winning move must be a declared kind (wrap/mirror)
  WinWithin,      // win within N of your moves
  Survive,        // do not lose for N moves
  Puzzle,         // a single line: the unique winning move / mate-in-N
};

struct Scenario {
  std::string variant;      // "torus", "t6", ...
  ObjectiveKind objective{ObjectiveKind::Win};
  int limit{0};             // N for WinWithin / Survive / Puzzle depth
  std::string aiProfile;    // which handicap the opponent plays under
  std::string startFenN;    // the position, if it is a puzzle
};

}
```

- **The opponent is handicapped by declaration, not by hoping the AI is weak.** Keys off
  M10's derived baseline and a small set of honest profiles: `full` (no handicap),
  `material` (the boss starts down material), `first-move` (the player moves twice in the
  opening), `depth-capped` (search depth pinned, and *stated in the UI* so the player is
  never lied to). A capped boss is fine **if it says it is capped**.
- **Objectives use the variant's own vocabulary** (`WinWithMove` compiles to "the winning
  move crossed an identification or reversed through a mirror", read from `MovePath` — the
  same trace M11 and the animation already consume). No per-boss rules code.
- **Every objective check is a predicate over engine state**, testable headlessly through
  the action log.

## M15.3 The final boss — the `t6` puzzle finale

The climax is a **curated position on the six-dimensional torus** with a unique winning
line, not an open game:

- A `Puzzle` scenario: a mate-in-N (small N) or a forced win before the timeline/torus can
  be exploited, authored once and **proved by the engine's own search** (M10) to have
  exactly one winning line at the declared depth.
- The player is told the objective in plain words ("mate in 3 on the six-torus") and gets
  the move camera (M11) to see the wrap. The boss is hard because the *board* is hard, not
  because a search is deep — which is exactly what makes it fair and what makes it a
  trailer.
- **Determinism:** the position and its unique line are golden-tested. If a rules or
  geometry change invalidates the puzzle, a test fails rather than shipping a boss with no
  solution (or two).

## M15.4 Per-variant victory policy (the existing pattern)

Some boards have no chess-style "win". "Defeat the boss here" must therefore be a
**declared per-scenario objective**, exactly as "what is forward for a pawn on a Klein
bottle" is a per-variant policy (ARCH §4.1, M3). M15 adds no new win conditions to the
engine; it adds scenarios that state their own, drawn from what the variant already
supports. A variant that cannot express a boss objective is simply not on the ladder.

## M15.5 Content as data, and the rot guard

- The whole ladder is a data file (a list of `Scenario`s), loaded like a variant. It is
  **not** C++ levels.
- **Every scenario is validated at load and in CI against the live engine:** the FEN-N
  parses, the objective predicate is reachable, and a `Puzzle` has a unique winning line
  at its stated depth. A rule or geometry change that breaks a scenario fails a test.
- The ladder is **not** part of `VariantId` and never enters the engine; it is `app`/`io`
  content over existing variants.

## M15.6 Tests and acceptance

- **Every scenario loads and is solvable:** for each `Puzzle`, the engine's search finds
  exactly one winning line at the declared depth; for each `WinWithin`/`Survive`, a
  reference line exists.
- **Objectives are engine predicates:** a `WinWithMove` boss is won by a wrapping move and
  not by a quiet one, asserted headlessly.
- **Handicaps are declared:** the UI text for a `depth-capped` boss says the cap; a test
  asserts every profile has player-visible text.
- **Determinism:** the final boss position and its unique line are a golden; replaying it
  reproduces hash-for-hash.
- **`--shot`:** every campaign screen (leg select, boss intro, victory/defeat) is
  captured and validation-clean.

### Acceptance facts

1. A player can complete the ladder from `standard` to the `t6` finale, and each leg
   teaches exactly one named concept.
2. No boss requires beating an unrestricted engine; every opponent plays under a declared
   profile, and a capped profile says so.
3. The final boss is a puzzle with a unique engine-proved winning line, golden-tested.
4. Every scenario is data, validated against the live engine, and a rule/geometry change
   that breaks one fails a test.
5. Victory conditions are per-scenario and drawn from the variant's own vocabulary; the
   engine gains no new win condition.

## Risks and non-goals

- **The strength temptation.** The instinct to make the final boss "the strongest AI"
  reintroduces exactly the unfairness and the M10 non-promise this design avoids. Bosses
  test understanding.
- **Content rot.** Puzzle positions age badly when geometry or rules change; the CI
  validation is the mitigation, and it is mandatory.
- **Scope.** The campaign is the **on-ramp, not the product**; retention is skirmish and
  the Workshop. Keep the ladder to a handful of legs; do not grow it into a level factory.
- **Non-goals:** no narrative engine, no cutscenes beyond static cards, no new variants
  invented just for the campaign (use what exists or none), no engine changes.

## Status

Planned, not started. Depends on M10 (the opponent and the puzzle solver) and M14 (the
on-ramp it extends); it is in Release Wave 3 (the roadmap), after the AI, because a boss
needs something to play against. It reuses M11 (the camera that shows the wrap) and the
existing variants throughout; the only new content is the scenario file.
