# ADR-0002: CMake + Catch2 v3, hermetic nix flake

- **Status:** Accepted
- **Date:** 2026-09-28

## Context
The project needs reproducible builds for humans, agents, and CI; heavy TDD
tooling; and eventual integration with Steam, IDEs, and third-party C++ deps.

## Decision
CMake (C++23, presets) as the build system; Catch2 v3 as the test framework; a
nix flake as the single source of the toolchain, with `nix flake check` running
the entire quality gate. One CMake target per architectural layer, with link
edges that make the layer map (ARCH §1) a build-time constraint.

## Consequences
CMake is verbose but is the only build system with universal ecosystem support.
Catch2 gives sections, generators (used heavily for dimension sweeps) and built-in
benchmarking in one dependency. `nix flake check` means CI cannot drift from local.
Nix is a barrier for contributors unfamiliar with it, mitigated by documenting a
plain-CMake path in the README.

## Alternatives considered
- **Meson:** cleaner, worse ecosystem integration.
- **Bazel:** great caching, poor fit for Steam/IDE/C++ third-party reality.
- **GoogleTest:** better mocking, but mocking is not what this project needs;
  generators and benchmarks are.
- **doctest:** faster compiles, missing the generator/benchmark features.

## How to reverse this
Test framework: mechanical, days. Build system: weeks. Nix: removable without
touching source.
