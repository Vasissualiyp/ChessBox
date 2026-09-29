# ADR-0011: The renderer is headless first; the window is a blit

- **Status:** Accepted
- **Date:** 2026-09-28

## Context

Graphics is the least testable part of a project like this, and the usual result is a
renderer verified by someone looking at it. That fails badly here for two reasons: the
thing being drawn is an N-dimensional lattice that nobody has an intuition for, so "it
looks right" is not a judgement anyone can make; and the project's whole method
(ADR-0009) is differential and property testing, which needs an oracle and an
assertion, not a screenshot.

## Decision

- **The primary render path has no window, no surface and no swapchain.** A
  `VulkanContext` is created headless, a frame is rendered into an `OffscreenTarget`,
  and the pixels are read back to host memory. Tests assert on them.
- **Validation layers are on, every message is recorded, and a message fails a test.**
  Warnings count as errors: a warning is a bug not yet understood.
- **The window presents by blitting the offscreen image.** It does not render into a
  swapchain image. There is therefore exactly one rendering path in the project, and it
  is the one the tests exercise - what a player sees is what was checked.
- **Picking is a CPU ray-box test against the same layout the renderer drew**, not an
  instance-id readback as originally planned. Cells are axis-aligned boxes at known
  positions, so the test is exact, it needs no GPU, and the entire click-to-select path
  is therefore testable headlessly.
- **The camera and the projection live in `view`, below the renderer**, so framing,
  layout and picking are all testable - and usable by a front end - with no graphics
  device present.

## Consequences

The renderer's correctness is machine-checked: every shipped variant renders with zero
validation messages, rendering is asserted bit-identical between runs, and the
projection round-trip (project a cell, pick it back) is a property test. A contributor
with no GPU still builds and passes everything except the handful of tests that skip
themselves.

Costs: an extra full-image blit per frame, which is irrelevant for a turn-based game
that redraws on input; and picking is O(cells) per click, which is microseconds even at
a million cells.

The deeper benefit showed up immediately - the first frame ever rendered produced a
validation error for a missing depth-image layout transition, a bug that would have
drawn a plausible picture on this driver and garbage on another.

## Alternatives considered

- **Swapchain-only rendering, verified by eye.** Standard practice, and untestable.
- **Instance-id attachment for picking.** Exact and GPU-side, but it makes the click
  path depend on a device and a readback, so it could not be tested headlessly.
- **Golden images.** Rejected for now: they are driver- and rasteriser-dependent, so
  they would either be flaky or pinned to one machine. The current assertions - zero
  validation messages, coverage, distinct-colour counts, run-to-run determinism, and
  "this change must alter the image" - are weaker per test but robust everywhere.

## How to reverse this

The window already owns its own context and swapchain; rendering directly into a
swapchain image would be a change to `Window::present` and the renderer's target
argument. Nothing else depends on the blit.
