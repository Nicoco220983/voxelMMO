#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$SCRIPT_DIR/.."
BUILD_DIR="$ROOT_DIR/build"
PRODUCTION="${PRODUCTION:-0}"

# ── Sanity checks ───────────────────────────────────────────────────────────
if [[ ! -f "$BUILD_DIR/voxelmmo" ]]; then
    echo "ERROR: Server binary not found at $BUILD_DIR/voxelmmo"
    echo "Run: bash scripts/build.sh"
    exit 1
fi
if [[ "$PRODUCTION" != "1" && ! -d "$ROOT_DIR/client/dist" ]]; then
    echo "ERROR: Client dist not found. Run: bash scripts/build.sh"
    exit 1
fi

# Wait for a port to be listening with timeout
wait_for_port() {
    local port=$1
    local timeout=${2:-30}
    local start_time=$(date +%s)
    
    echo "Waiting for port $port..."
    while ! ss -tlnp 2>/dev/null | grep -q ":$port "; do
        local current_time=$(date +%s)
        if (( current_time - start_time > timeout )); then
            echo "ERROR: Timeout waiting for port $port"
            return 1
        fi
        sleep 0.1
    done
    echo "Port $port is ready"
}

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
    # Don't kill - server handles SIGINT itself and saves chunks
    # Just wait for processes to exit
    wait "$SERVER_PID" "$CLIENT_PID" 2>/dev/null || true
  }
  trap cleanup INT TERM EXIT

  # Start game server
  "$BUILD_DIR/voxelmmo" "$@" &
  SERVER_PID=$!
  
  # Wait for gateway to be ready
  if ! wait_for_port 8080; then
      echo "ERROR: Game server failed to start on port 8080"
      exit 1
  fi

  # Start vite preview (use --strictPort to fail if 3000 is taken)
  cd "$ROOT_DIR/client"
  npx vite preview --port 3000 --strictPort &
  CLIENT_PID=$!
  
  # Wait for vite to be ready
  if ! wait_for_port 3000; then
      echo "ERROR: Vite preview failed to start on port 3000"
      exit 1
  fi

  echo "=== voxelmmo dev — http://localhost:3000 (Ctrl+C to stop) ==="
  wait

fi
