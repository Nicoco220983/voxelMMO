#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."

# ── Determine build mode ──────────────────────────────────────────────────
BUILD_MODE="${BUILD_MODE:-Release}"
BUILD_DIR="$ROOT_DIR/build/$BUILD_MODE"

# ── Sanity check ────────────────────────────────────────────────────────────
if [[ ! -f "$BUILD_DIR/voxelmmo_tests" ]]; then
    echo "ERROR: Test binary not found at $BUILD_DIR/voxelmmo_tests"
    echo "Build with: BUILD_MODE=$BUILD_MODE bash scripts/build.sh"
    exit 1
fi

# ── C++ tests (Catch2) ────────────────────────────────────────────────────
echo "=== Running C++ tests (${BUILD_MODE}) ==="
"$BUILD_DIR/voxelmmo_tests"

# ── Client JS tests (vitest) ────────────────────────────────────────────────
echo ""
echo "=== Running client JS tests ==="
cd "$ROOT_DIR/client"
npm test

echo ""
echo "=== All tests passed ==="
