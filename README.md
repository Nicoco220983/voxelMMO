# voxelMMO

A high-performance browser-based massively multiplayer voxel game on a wide procedurally generated world.

## Stack

| Layer | Tech |
|---|---|
| Server | C++20, entt ECS, uWebSockets, LZ4 |
| Build | CMake + vcpkg + Ninja |
| Client | Vanilla JS (JSDoc typed), Three.js, lz4js, Vite |
| Proxy | nginx — static files + WebSocket proxy |

## Architecture

- **Authoritative server** using an entt C++ ECS.
- **Two engines**: `GameEngine` (game logic) and `GatewayEngine` (WebSocket, uWS) — can run on separate machines, communicating via Unix socket or TCP.
- **Chunked world**: 64×16×64 voxel chunks, activated/deactivated as players move. Generated on first activation.
- **Three message levels** per chunk: snapshot, snapshot delta, tick delta — all sharing a common 13-byte header (type + ChunkId + tick).
- **LZ4 compression** throughout: voxel snapshots are zero-copy compressed; deltas are compressed in-place above a size threshold.

## Getting started

**Prerequisites:** Ubuntu/Debian, `git`, `curl`.

```bash
# 1. Install system deps + vcpkg + Node.js + npm packages
./scripts/install.sh

# 2. Build server (C++) and client (Vite)
./scripts/build.sh

# Optional: debug build with ASan + UBSan
./scripts/build.sh --debug
```

## Running

```bash
# Development (no sudo — game server + vite preview at http://localhost:3000)
./scripts/start.sh

# Production (deploys via nginx, requires sudo)
PRODUCTION=1 ./scripts/start.sh
```

## Project layout

```
server/   C++ server (game + gateway engines, ECS components, chunk serialisation)
client/   Vanilla JS client (Three.js rendering, chunk/entity state, WebSocket)
nginx/    nginx.conf for production deployment
scripts/  install.sh, build.sh, start.sh
tests/    Server-side tests
```
