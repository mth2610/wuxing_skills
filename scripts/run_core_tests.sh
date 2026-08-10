#!/bin/bash
# Builds and runs the core headless test suite (core/tests/*_test.c).
#
# Modelled on the rlvk ladder (scripts/run_rlvk_runtime_test.sh): each test is a
# standalone C file that links NOTHING from the game — no raylib, no GL, no
# window. That is the whole point: it runs in milliseconds, needs no display, and
# can be pointed at questions that are really arithmetic rather than rendering.
#
# Tier 1 (compile)  — not yet built for core.
# Tier 2 (headless) — this script. Numeric/logic assertions, no GPU.
# Tier 3 (visual)   — not yet built; would drive the real app and diff
#                     screenshots (see AutoTest_SaveScreenshot).
#
# Usage:
#   ./scripts/run_core_tests.sh              # run everything
#   ./scripts/run_core_tests.sh particle     # run tests whose name matches
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${CORE_TEST_OUT:-/tmp/wuxing_core_tests}"
FILTER="${1:-}"
mkdir -p "$OUT"

cd "$ROOT"   # tests read repo-relative paths (e.g. core/shaders/*.fs)

total=0; failed=0; failed_names=()

for src in "$ROOT"/core/tests/*_test.c; do
    [ -e "$src" ] || { echo "no tests found in core/tests/"; exit 1; }
    name="$(basename "$src" .c)"
    if [ -n "$FILTER" ] && [[ "$name" != *"$FILTER"* ]]; then continue; fi

    total=$((total + 1))
    bin="$OUT/$name"

    # core/tests/stubs is searched LAST and holds plain-data stand-ins only (see
    # its raylib.h): a core header that is pure arithmetic may still include
    # raylib.h for a struct, and without the stub that test fails to BUILD —
    # which reads like a passing suite in a 47-line summary.
    if ! cc "$src" -o "$bin" -I"$ROOT" -I"$ROOT/core/tests/stubs" \
            -std=c11 -Wall -Wextra -lm 2>"$OUT/$name.build.log"; then
        echo "=== $name: BUILD FAILED ==="
        cat "$OUT/$name.build.log"
        failed=$((failed + 1)); failed_names+=("$name (build)")
        continue
    fi

    if ! "$bin"; then
        failed=$((failed + 1)); failed_names+=("$name")
    fi
    echo
done

echo "========================================"
if [ "$total" -eq 0 ]; then
    echo "no tests matched filter '${FILTER}'"
    exit 1
fi
if [ "$failed" -eq 0 ]; then
    echo "core tests: $total/$total suites passed"
else
    echo "core tests: $((total - failed))/$total suites passed"
    for n in "${failed_names[@]}"; do echo "  FAILED: $n"; done
fi
exit $([ "$failed" -eq 0 ] && echo 0 || echo 1)
