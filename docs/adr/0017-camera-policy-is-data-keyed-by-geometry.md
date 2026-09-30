# ADR-0017 — Camera policy is data, keyed by geometry kind, not by dimension

**Status:** Accepted

## Context

A move camera has to make choices the engine cannot make for the player: whether to follow
the piece or frame the destination, how far to lead it, how a portal crossing is shown
(cut, fade, orbit), whether a mirror stops the camera, whether a leap is followed or framed,
and whether a move across a grid axis pans or cuts. These are *taste and legibility*, not
rules, and they differ by geometry and view style - an orbit shows a Klein turn well and a
long 6-D dolly badly.

They are not a function of the dimension count. A 3-D box and a 4-D box want the same
choices; a torus and a cylinder want the same portal treatment; a mirror is special whether
it is in 2 or 6 dimensions. Keying policy on `dims` would be the same mistake as keying
movegen on it.

## Decision

`CameraPolicy` is a small data record (`view/move_camera.hpp`) with sane defaults:

```
follow, lead, pull, minDistance, deadline,
portal, bounce, leap, grid
```

- The defaults reproduce today's behaviour exactly (`follow = Route` still returns the
  settled pose at the endpoints; `follow = Off` is a strict no-op), so the change is a
  superset.
- Defaults are keyed off data the view already has: `SeamKind` for the portal look,
  `ViewConfig` for whether an axis is a screen or grid axis, and `MovePath::leap` - **never**
  a dimension count.
- The record is intended to become per-variant data (M12's authored `[camera]` block) and to
  travel through the M9 package. It is **cosmetic**: it must not enter `VariantId`, exactly
  as piece `shape`/`height` do not.
- No policy branch may test the dimension count; a test asserts this.

## Consequences

- A new geometry extends the policy table, not the code; a new view style is a different
  policy, not a different camera.
- The camera's behaviour is data, so it round-trips and can be authored and shared.
- Two variants with the same geometry can still be tuned differently without a code branch.
- Policy must be threaded through to the camera's callers; today it defaults, and the
  settings/authoring paths (M11.5/M12.4) fill it in.

## Alternatives considered

- **Hard-code the choices in `moveCamera`.** Rejected: un-authorable, untestable per
  geometry, and impossible to ship as a preset.
- **Key policy on `dims`.** Rejected: dimension is not what makes a camera readable; the
  geometry and the view style are.
- **A single global constant.** Rejected: a torus and a mirror want different portal and
  bounce handling, and an author will want a say.

## How to reverse this

The policy record has one consumer (`moveCamera`) and defaults everywhere; replacing it with
constants is a local change in `move_camera.cpp` plus the settings/authoring call sites.
