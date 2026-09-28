# ADR-0006: SDL3 + Vulkan 1.3

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
The renderer must draw up to millions of cells across N-D projections, at
interactive rates, on Linux/Windows/macOS and Steam Deck. Scene content is highly
uniform (instanced cells and pieces) — exactly the shape that rewards explicit
control over buffers and descriptors.

## Decision
SDL3 for window/input; Vulkan 1.3 core with dynamic rendering and
`synchronization2`; VMA for allocation; shaderc at build time to SPIR-V. One
instanced draw per piece type / cell-state class. Validation layers on in Debug,
and validation errors fail tests.

## Consequences
Highest performance ceiling and predictable frame costs; ~3000 lines before the
first triangle, and a slower path to the first visible board — which is why the
renderer is M4 and a TUI viewer lands in M2 for early visibility. macOS requires
MoltenVK (an accepted, documented cost). RenderDoc integration is excellent,
which matters for debugging N-D projections.

## Alternatives considered
- **SDL3 + OpenGL 4.5:** far less code, adequate performance, but a lower ceiling
  and worse tooling; would likely be replaced later anyway.
- **bgfx:** less boilerplate, multi-backend, but a large dependency and its own
  shader toolchain.
- **raylib/Magnum:** fastest to a demo; their abstractions obstruct custom
  instanced N-D rendering.

## How to reverse this
The renderer sits behind the `PositionView` snapshot boundary (ARCH §10), so the
backend is replaceable without engine changes.
