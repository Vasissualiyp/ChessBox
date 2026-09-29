# M8 — Client–Server Multiplayer

**Goal:** authoritative server, thin clients, determinism-verified. Turn-based
games do not need rollback netcode; they need airtight validation and
reconnection.

**Exit condition:** two clients play every shipped variant class across a lossy
link, with server-side validation and full replay reproduction.

## M8.1 Architecture

- Authoritative server owns the `Game`; clients send *intents*, receive
  validated state deltas. A client never advances state on its own authority
  (it may predict for responsiveness, but the server's word is final).
- The engine core is already the server: no separate rules implementation ever
  exists. **[INVARIANT]**
- Transport: TCP with length-prefixed framed messages, TLS. No UDP — turn-based.
- Protocol messages are schema-defined, versioned, and code-generated or
  hand-written against a schema test; forward/backward compatibility rules
  documented and tested.

## M8.2 Determinism and identity

- Clients and server must agree on `VariantId` (ARCH §11) before a game starts;
  mismatch is a clean, explained refusal, never a desync.
- Every state delta carries the resulting position hash; a client detecting a
  mismatch dumps a diagnostic bundle (variant, replay, hashes) and disconnects
  loudly rather than silently diverging. **[INVARIANT]**
- The replay corpus runs in CI through the network layer, not just in-process.

## M8.3 Features

- Lobby, matchmaking-lite (direct challenge + public list), spectators,
  reconnection with full state resync, per-player clocks (integer milliseconds;
  no floats), resign/draw offers/takebacks as protocol actions, server-side
  game persistence and replay export.
- Variant transfer: a client missing a variant receives the (hashed, validated)
  variant package from the server, subject to M9's validation rules.

## M8.4 Testing

- In-process transport fake for deterministic protocol tests (the default; fast).
- A network harness injecting latency, loss, reordering, truncation, and
  mid-message disconnects.
- Adversarial client tests: malformed frames, oversized messages, illegal moves,
  out-of-turn moves, replayed messages, protocol-state violations, slow-loris
  connections. Each is a named test. **[INVARIANT]**
- Fuzz the message decoder.
- Soak test: N concurrent games for hours under ASan, asserting no leaks and
  bounded memory.

## Acceptance facts

1. Two clients complete games in every shipped variant class (2-D, N-D, exotic
   geometry, VM-heavy, temporal) over a lossy link.
2. Reconnection restores full state including custom fields and timelines.
3. Every adversarial-client test is rejected cleanly with no server crash, hang,
   or unbounded allocation.
4. Hash mismatch produces a diagnostic bundle and a loud failure, never silent
   divergence.
5. Replay corpus reproduces through the network path.
6. Soak test clean under ASan.
