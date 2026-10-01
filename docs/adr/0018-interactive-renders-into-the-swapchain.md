# ADR-0018: The interactive path renders into the swapchain; two frames in flight

- **Status:** Accepted
- **Date:** 2026-09-30

## Context

ADR-0011 made the renderer headless-first and, for the windowed front end, present by
blitting an offscreen image. That kept a single rendering path - the one the tests
exercise - at the cost of one full-image copy per interactive frame, and it left the
interactive path fully serialised: the board render submitted and waited on a fence, then
the window acquired, blitted, presented and waited again. M4.1 had promised "frames in
flight = 2"; what shipped was one frame in flight, with the CPU and GPU strictly taking
turns.

The gain from fixing it is input-to-photon latency, not frame rate: under the default
FIFO vsync neither change raises frames per second. But the renderer had grown a
multisample attachment, a scratch image for the defocus pass, and a per-image present
sequence, and the blit was now a visible per-frame cost with no remaining justification.

## Decision

- **The interactive path renders straight into the acquired swapchain image.** Each
  swapchain image gets a render target that wraps it as the colour attachment and owns
  its own multisample, depth and scratch images. The board, the defocus pass and the
  interface draw into the swapchain image directly; `Window::present` no longer blits.
- **The colour format is a runtime property, not a constant.** The pipelines are built
  for the surface's format (usually `B8G8R8A8_UNORM`), because a multisample resolve and
  a dynamic-rendering pipeline both require the attachment format to match.
- **Two frames may be in flight.** `Window` owns a two-slot frame ring - a command
  buffer, a fence and an acquire semaphore per slot - and the renderer records into the
  caller's command buffer without submitting. The per-frame instance buffer and the blur
  descriptor sets are per slot, so frame N+1 cannot overwrite data frame N is still
  reading. A slot's fence is waited before it is reused; no full-queue wait remains on the
  interactive path.
- **The offscreen path is unchanged for captures and tests.** `--shot`, `--clip` and the
  whole render test suite still render into an `OffscreenTarget` and read it back. Both
  paths share one renderer, one shader set and one instance builder, so the board the
  tests check is the board the window draws.
- **The renderer records; the window submits.** `BoardRenderer::record` takes a command
  buffer the caller owns, so a test can drive the exact interactive recording path with no
  window, and so the frame ring can stay in `Window` where the swapchain is.

## Consequences

The per-frame full-image blit is gone, and the CPU records the next frame while the GPU
draws the last. The renderer and the interface no longer hardcode one format.

Costs. The interactive present sequence is now the one part of the renderer with **no
headless test**: `vkAcquireNextImageKHR` and `vkQueuePresentKHR` need a surface. The
recording half of the path - the per-image target, the dynamic format, both frame slots -
is covered by a headless test that renders into an image with the same usages as a
swapchain image; only the acquire/present calls themselves are caught by a human running
the game. A resize or a surface-format change rebuilds the swapchain and its targets, and
the pipelines would have to be rebuilt if the format changed, which in practice surfaces
do not do across a resize.

## Alternatives considered

- **Keep the blit and pipeline only the present.** Simpler, but leaves the per-frame
  copy and does not remove the offscreen render's serialisation; the plan's second bullet,
  rendering into the swapchain image, is the one that makes the first cheap.
- **Render into a per-swapchain-image offscreen image and blit it.** Same number of
  images, and keeps the blit: no.
- **Add a second pipeline set for the swapchain format.** Would let the offscreen format
  stay fixed, but doubles every pipeline for no benefit; making the format a runtime
  parameter is a smaller, single change.

## How to reverse this

`Window::present` (the blit) can be restored from history; the offscreen target still
produces the image it needs, and ADR-0011's one-path argument becomes true again. The
frame ring is confined to `Window`, and the renderer's `record`/`render` split means a
synchronous caller is unaffected.
