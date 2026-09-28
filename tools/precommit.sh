#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run before every commit. Cheapest checks first, so a failure costs seconds.
set -euo pipefail
cd "$(dirname "$0")/.."

fail() { echo "precommit: $1" >&2; exit 1; }

echo "== format =="
mapfile -t files < <(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(hpp|cpp)$' || true)
if [[ ${#files[@]} -gt 0 ]]; then
  clang-format --dry-run --Werror "${files[@]}" || fail "clang-format found issues (run: clang-format -i <files>)"
fi

echo "== configure and build (dev) =="
cmake --preset dev >/dev/null
cmake --build build/dev

echo "== arch tests =="
ctest --preset dev -L arch

echo "== unit tests =="
ctest --preset dev -L unit

echo "== property tests =="
ctest --preset dev -L property

echo "precommit: OK. Before a PR, also run the full gate: nix flake check"
