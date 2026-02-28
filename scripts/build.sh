#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT_DIR/build"

export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"

# ── Server (C++ via CMake + vcpkg) ────────────────────────────────────────
echo "=== Building server ==="
cmake -B "$BUILD_DIR" -S "$ROOT_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -G Ninja

cmake --build "$BUILD_DIR" --config Release

# ── Client (Vite) ─────────────────────────────────────────────────────────
echo "=== Building client ==="
cd "$ROOT_DIR/client"
npm run build

echo ""
echo "=== Build complete ==="
echo "  Server binary : $BUILD_DIR/voxelmmo"
echo "  Client dist   : $ROOT_DIR/client/dist/"
