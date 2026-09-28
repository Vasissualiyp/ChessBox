# ADR-0008: GPL-3.0-or-later

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
The project is explicitly FOSS. It will also ship on Steam and run a multiplayer
server, and it will integrate the proprietary Steamworks SDK.

## Decision
GPL-3.0-or-later. `LICENSE` holds the verbatim GPL-3.0 text; every source file
carries an `SPDX-License-Identifier: GPL-3.0-or-later` header, checked by a test.

## Consequences
Derivatives and forks stay free. Compatible with selling on Steam, though anyone
may rebuild and redistribute. **Two issues to handle explicitly:**
1. Linking the proprietary Steamworks SDK into GPL code needs an additional
   permission (a linking exception) from the copyright holders, or Steam support
   must live in a separate, optional, non-GPL-linked component. Addressed in
   `docs/LICENSING.md` during M8; the M8 plan already isolates Steam behind an
   interface with a working Steam-absent path, which keeps both options open.
2. GPL (unlike AGPL) does not reach the hosted server case; accepted knowingly.

Contributor licensing: inbound = outbound, recorded in `CONTRIBUTING.md`.

## Alternatives considered
- **MIT/Apache-2.0:** wider adoption, permits closed forks; rejected as contrary
  to the project's intent.
- **MPL-2.0:** file-level copyleft, weaker guarantee.
- **AGPL-3.0:** closes the server loophole, but makes third-party server hosting
  and Steamworks integration harder still.

## How to reverse this
Relicensing requires agreement from every copyright holder, so this is effectively
one-way. Keep the contributor record clean from the first commit.
