#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT_DIR/build"

# ── C++ tests (Catch2) ────────────────────────────────────────────────────────
echo "=== Running C++ tests ==="
"$BUILD_DIR/voxelmmo_tests"

# ── Client JS tests (vitest) ──────────────────────────────────────────────────
echo ""
echo "=== Running client JS tests ==="
cd "$ROOT_DIR/client"
npm test

echo ""
echo "=== All tests passed ==="
