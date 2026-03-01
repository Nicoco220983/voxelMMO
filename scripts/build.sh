#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT_DIR/build"

export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"

# ── Parse flags ───────────────────────────────────────────────────────────
BUILD_TYPE=Release
EXTRA_FLAGS=""
for arg in "$@"; do
  case "$arg" in
    --debug)
      BUILD_TYPE=Debug
      EXTRA_FLAGS="-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined"
      ;;
  esac
done

# ── Server (C++ via CMake + vcpkg) ────────────────────────────────────────
echo "=== Building server (${BUILD_TYPE}) ==="
cmake -B "$BUILD_DIR" -S "$ROOT_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -G Ninja \
  ${EXTRA_FLAGS:+"$EXTRA_FLAGS"}

cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"

# ── Client (Vite) ─────────────────────────────────────────────────────────
echo "=== Building client ==="
cd "$ROOT_DIR/client"
npm run build

echo ""
echo "=== Build complete ==="
echo "  Server binary : $BUILD_DIR/voxelmmo"
echo "  Client dist   : $ROOT_DIR/client/dist/"
