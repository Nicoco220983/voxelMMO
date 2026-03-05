#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT_DIR/build"
PRODUCTION="${PRODUCTION:-0}"

if [[ "$PRODUCTION" == "1" ]]; then

  # ── Production: deploy via nginx (requires sudo) ──────────────────────────
  sudo mkdir -p /var/www/voxelmmo
  sudo cp -r "$ROOT_DIR/client/dist/." /var/www/voxelmmo/

  sudo cp "$ROOT_DIR/nginx/nginx.conf" /etc/nginx/nginx.conf
  sudo nginx -t
  sudo systemctl reload-or-restart nginx

  echo "=== Starting voxelmmo (Ctrl+C to stop) ==="
  exec "$BUILD_DIR/voxelmmo"

else

  # ── Development: no sudo, vite preview + game server in background ────────
  cleanup() {
    echo ""
    echo "=== Stopping voxelmmo ==="
    kill "$SERVER_PID" "$CLIENT_PID" 2>/dev/null || true
    wait "$SERVER_PID" "$CLIENT_PID" 2>/dev/null || true
  }
  trap cleanup INT TERM EXIT

  "$BUILD_DIR/voxelmmo" "$@" &
  SERVER_PID=$!

  cd "$ROOT_DIR/client"
  npx vite preview &
  CLIENT_PID=$!

  echo "=== voxelmmo dev — http://localhost:3000 (Ctrl+C to stop) ==="
  wait

fi
