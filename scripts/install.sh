#!/usr/bin/env bash
set -euo pipefail

echo "=== voxelmmo: installing dependencies ==="

# ── vcpkg ─────────────────────────────────────────────────────────────────
if [ ! -d "${VCPKG_ROOT:-$HOME/vcpkg}" ]; then
  git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
  "$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
fi
export VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"

# ── System packages ───────────────────────────────────────────────────────
sudo apt-get update -y
sudo apt-get install -y \
  cmake \
  ninja-build \
  build-essential \
  pkg-config \
  nginx

# ── Node.js (via nvm if not present) ─────────────────────────────────────
if ! command -v node &>/dev/null; then
  curl -fsSL https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash
  export NVM_DIR="$HOME/.nvm"
  # shellcheck source=/dev/null
  source "$NVM_DIR/nvm.sh"
  nvm install --lts
fi

# ── Client npm dependencies ───────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/../client"
npm install

echo "=== Install complete ==="
echo "  vcpkg: ${VCPKG_ROOT}"
echo "  Run scripts/build.sh to compile."
