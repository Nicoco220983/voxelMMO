#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT_DIR/build"

export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"

# ── C++ tests (Catch2) ────────────────────────────────────────────────────────
echo "=== Configuring C++ tests ==="
cmake -B "$BUILD_DIR" -S "$ROOT_DIR" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -G Ninja

echo "=== Building C++ tests ==="
cmake --build "$BUILD_DIR" --config Debug --target voxelmmo_tests

echo "=== Running C++ tests ==="
"$BUILD_DIR/voxelmmo_tests"

# ── Client JS tests (vitest) ──────────────────────────────────────────────────
echo ""
echo "=== Installing client dependencies ==="
cd "$ROOT_DIR/client"
npm install

echo "=== Running client JS tests ==="
npm test

echo ""
echo "=== All tests passed ==="
