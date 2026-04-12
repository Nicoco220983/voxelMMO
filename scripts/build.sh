#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."

export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"

# ── Determine build mode ──────────────────────────────────────────────────
# Priority: --debug flag > BUILD_MODE env var > default (Release)
BUILD_MODE="${BUILD_MODE:-Release}"
EXTRA_FLAGS=""

for arg in "$@"; do
  case "$arg" in
    --debug)
      BUILD_MODE=Debug
      EXTRA_FLAGS="-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined"
      ;;
  esac
done

BUILD_DIR="$ROOT_DIR/build/$BUILD_MODE"

# ── Server (C++ via CMake + vcpkg) ────────────────────────────────────────
echo "=== Building server (${BUILD_MODE}) ==="
echo "  Build directory: $BUILD_DIR"

cmake -B "$BUILD_DIR" -S "$ROOT_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_MODE" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -G Ninja \
  ${EXTRA_FLAGS:+"$EXTRA_FLAGS"}

cmake --build "$BUILD_DIR" --config "$BUILD_MODE"

# ── Client (Vite) ───────────────────────────────────────────
echo "=== Building client ==="
cd "$ROOT_DIR/client"
npm run build

echo ""
echo "=== Build complete ==="
echo "  Mode          : $BUILD_MODE"
echo "  Server binary : $BUILD_DIR/voxelmmo"
echo "  Client dist   : $ROOT_DIR/client/dist/"
echo ""
echo "To run: BUILD_MODE=$BUILD_MODE bash scripts/start.sh"
