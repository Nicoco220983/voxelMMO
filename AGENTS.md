# voxelmmo

A high performance online webgame massive multiplayer, on wide generated world.

# Stack

- Server: C++20, entt ECS, uWebSockets, LZ4 — built with CMake + vcpkg + Ninja
- Client: vanilla JS (JSDoc typed, `// @ts-check`), Three.js, lz4js — bundled with Vite
- nginx: static files + WebSocket proxy to gateway port 8080

# Architecture

- Authoritative server with entt C++ ECS. No game logic in the gateway.
- GameEngine (game loop) and GatewayEngine (uWS) are decoupled; can run on separate servers.
- World is fully chunked. Chunks activate on first player approach, generate terrain on activation.
- Each Chunk owns a ChunkState (snapshot + deltas + scratch); serialisation is per-chunk and parallelisable.
- 3 message levels per chunk: snapshot → snapshot delta → tick delta. See `docs/wire-format.md`.

# Core principles

- **Pure ECS**: all entity state lives in entt components. No parallel data structures mirroring the registry.
- **Fixed-point positions**: `int32_t` sub-voxels, 1 voxel = `SUBVOXEL_SIZE` (256) units. No floats in game state.
  - `CHUNK_SHIFT_Y/X/Z = 12/14/14` → chunk coord = position >> shift (works directly on sub-voxel values).
  - Client sends button bitmask + yaw/pitch; server `InputSystem` converts to velocity each tick.
  - Rendering: divide by `SUBVOXEL_SIZE` before passing to Three.js.
- **Dirty flags**: `DirtyComponent` carries `snapshotDirtyFlags` and `tickDirtyFlags` (1 bit per component).
  `modify(dirty=true)` marks both; cleared after the matching delta is sent.
- **ChunkEntityId** (uint16): per-chunk wire id, assigned on entry, freed on departure. Not a stable entity id.
- **Serialisation ownership**: each component's `serializeFields(BufWriter&)` writes its own bytes only.
  The caller (Chunk) writes the component-flags byte and decides which components to include.

# Key types

| Type | Repr | Notes |
|------|------|-------|
| ChunkId | int64 packed | sint6(y) · sint29(x) · sint29(z) |
| VoxelId | uint16 packed | uint4(y) · uint6(x) · uint6(z) |
| VoxelType | uint8 | 0 = air |
| ChunkEntityId | uint16 | per-chunk, per-residence |
| PlayerId | uint32 | persistent |
| GatewayId | uint32 | |

Chunk voxels: 64 × 16 × 64 = 65 536 bytes.

# Code structure

**server/common/**
- `Types.hpp` — ChunkId, VoxelId, VoxelType, ChunkEntityId, PlayerId, GatewayId; chunk dims; SUBVOXEL_SIZE, CHUNK_SHIFT_*; `GHOST_MOVE_SPEED=256`, `PLAYER_WALK_SPEED=77`, `PLAYER_JUMP_VY=110`; physics constants (`GRAVITY_DECREMENT`, `TERMINAL_VELOCITY`, `PLAYER_BBOX_HX/HY/HZ`)
- `MessageTypes.hpp` — ChunkMessageType, DeltaType, EntityType (PLAYER=0, GHOST_PLAYER=1), ClientMessageType (INPUT=0, JOIN=1), `InputButton` bitmask enum
- `ChunkState.hpp` — snapshot + deltas + scratch buffers; shared by Chunk and StateManager
- `BufWriter.hpp` — sequential write helper (`write<T>` via memcpy)
- `NetworkProtocol.hpp` — serialization helpers (parseInput, parseJoin, buildSelfEntityMessage, appendFramed)
- `VoxelTypes.hpp` — named voxel type constants (AIR=0, STONE=1, DIRT=2, GRASS=3)

**server/game/entities/**
- `PlayerEntity.hpp` — `spawn()` factory for PLAYER (PhysicsMode::FULL, grounded=false)
- `GhostPlayerEntity.hpp` — `spawn()` factory for GHOST_PLAYER (PhysicsMode::GHOST, grounded=true)
- `EntityFactory.hpp` — `playerFactories` map (`EntityType → PlayerSpawnFn`); pulled in by `GameEngine.hpp`

**server/game/GameEngine.hpp/cpp**
- `entt::registry registry` — single source of truth for all entity state
- `map[ChunkId, Chunk] chunks`, `map[GatewayId, GatewayInfo] gateways`, `map[PlayerId, entt::entity] playerEntities`
- `queuePendingPlayer()` — called on WebSocket connect; parks player in `pendingPlayers` until JOIN arrives
- `addPlayer()` — delegates to `playerFactories` map; accepts optional `EntityType` (default `GHOST_PLAYER`); used directly by tests
- `removePlayer()` — cleans chunk membership via ChunkMemberComponent, then destroys entity
- `teleportPlayer()` — directly sets player position (for test setup / admin use)
- `tick()` → `InputSystem::apply()` → `PhysicsSystem::apply()` → `checkEntitiesChunks()` → `serializeSnapshotDelta()` or `serializeTickDelta()`
- `checkEntitiesChunks()` — phase A: moves entities between chunks on `dyn.moved`; phase B: rebuilds watchedChunks, dispatches snapshots for newly seen chunks

**server/game/WorldGenerator.hpp/cpp**
- Stateless procedural terrain generator using multi-frequency simplex noise
- `generate(voxels, cx, cy, cz)` — fills voxel buffer for chunk
- `surfaceY(wx, wz)` — surface height at world position (matches generation logic)

**server/game/Chunk.hpp/cpp**
- `map[entt::entity, ChunkEntityId] entities` — chunk membership + wire id assignment (`nextChunkEntityId_`)
- `buildSnapshot(reg, tick)` — LZ4(voxels) zero-copy + entity section into scratch, compress if above threshold
- `buildSnapshotDelta / buildTickDelta` — staging buffer → optional LZ4 → appended to state.deltas

**server/game/WorldChunk.hpp/cpp**
- `voxels[65536]` — flat Y×X×Z voxel array
- `voxelsSnapshotDeltas`, `voxelsTickDeltas` — changed-voxel lists, cleared after each delta send

**server/game/components/**
- `DirtyComponent` — `snapshotDirtyFlags`, `tickDirtyFlags`; `mark(bit)`, `clearSnapshot()`, `clearTick()`
- `DynamicPositionComponent` — x,y,z,vx,vy,vz (int32 sub-voxels), grounded, moved; `modify()`; `serializeFields(BufWriter&)`
- `EntityTypeComponent` — `EntityType type`; emplaced on every entity at creation
- `InputComponent` — `buttons` (uint8 bitmask), `yaw`, `pitch` (float radians); updated by handlePlayerInput(); read by InputSystem
- `PlayerComponent` — `PlayerId playerId`; emplaced on player entities only
- `ChunkMemberComponent` — `currentChunkId`, `chunkAssigned`; managed by checkEntitiesChunks()
- `PhysicsModeComponent` — `PhysicsMode mode` (GHOST/FLYING/FULL); server-only, not serialised
- `BoundingBoxComponent` — AABB half-extents (hx, hy, hz) in sub-voxels; centered on position

**server/game/systems/**
- `InputSystem.hpp` — `apply(registry)`: translates InputComponent (buttons+yaw+pitch) → DynamicPositionComponent velocity per EntityType; called at top of `tick()` before physics
- `PhysicsSystem.hpp` — `apply(registry, chunks)`: collision-aware physics sweeps (X/Y/Z) with voxel-context cache; handles GHOST (no collision), FLYING (collision, no gravity), FULL (collision + gravity)

**server/gateway/**
- `GatewayEngine` — uWS server; player connect/disconnect/input callbacks; `receiveGameBatch()` forwards to clients
- `StateManager` — `map[ChunkId, ChunkState]`; routes chunk state messages to watching players

**client/src/**
- `types.js` — mirrors server enums + constants (ChunkMessageType, EntityType, SUBVOXEL_SIZE …)
- `utils.js` — `lz4Decompress`, `BufReader` (sequential binary read)
- `GameClient.js` — WebSocket; parses 13-byte header; dispatches to Chunk instances
- `Chunk.js` — per-chunk voxel state, LZ4 decompression, Three.js mesh rebuild
- `components/DynamicPositionComponent.js` — mirrors server; `predictAt(tick)` for client-side interpolation
- `entities/BaseEntity.js`, `PlayerEntity.js` — `fromRecord()`, `applyDelta()`
- `NetworkProtocol.js` — serialization helpers (serializeInput, serializeJoin, parseBatch, parseHeader)
- `main.js` — Three.js scene, render loop, HUD

**docs/**
- `wire-format.md` — chunk message binary layout (keep in sync with Chunk.cpp)

# AI agent workflow

> Keep **both** `AGENTS.md` ≤ 2.5 K tokens.
> Trim stale content before adding new content.

## Build

```bash
bash scripts/build.sh           # Release — server (C++) + client (Vite)
bash scripts/build.sh --debug   # Debug + ASan/UBSan
```

## Test

```bash
bash scripts/test.sh            # C++ unit tests (Catch2) + JS tests (vitest)
```

## Checklist after any structural change

1. `bash scripts/build.sh` — must compile cleanly with zero warnings.
2. `bash scripts/test.sh` — all tests must pass.
3. Update `AGENTS.md` (types, component list, architecture notes) and `docs/` as needed.
