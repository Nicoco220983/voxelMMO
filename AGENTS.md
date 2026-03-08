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
  - `CHUNK_SHIFT_Y/X/Z = 13/13/13` → chunk coord = position >> shift (works directly on sub-voxel values).
  - Client sends button bitmask + yaw/pitch; server `InputSystem` converts to velocity each tick.
  - Rendering: divide by `SUBVOXEL_SIZE` before passing to Three.js.
- **Dirty flags**: `DirtyComponent` carries `snapshotDirtyFlags` and `tickDirtyFlags` (1 bit per component).
  `modify(dirty=true)` marks both; cleared after the matching delta is sent.
- **GlobalEntityId** (uint32): assigned at spawn, stable across chunk moves and server lifetime. Used on wire.
- **Serialisation ownership**: each component's `serializeFields(BufWriter&)` writes its own bytes only.
  The caller (Chunk) writes the component-flags byte and decides which components to include.

# Key types

| Type | Repr | Notes |
|------|------|-------|
| ChunkId | int64 packed | sint6(y) · sint29(x) · sint29(z) |
| VoxelIndex | uint16 | uint5(y) · uint5(x) · uint5(z) — flat array index for deltas |
| VoxelType | uint8 | 0 = air |
| GlobalEntityId | uint32 | assigned at spawn, stable across moves |
| PlayerId | uint32 | persistent |
| GatewayId | uint32 | |

Chunk voxels: 32 × 32 × 32 = 32 768 bytes. Use `packVoxelIndex(x,y,z)` to compute flat index.

> **Note:** `ChunkEntityId` (uint16) is deprecated; `GlobalEntityId` is now used on the wire.

# Code structure

**server/common/**
- `Types.hpp` — ChunkId, VoxelId, VoxelType, GlobalEntityId, PlayerId, GatewayId; chunk dims; SUBVOXEL_SIZE, CHUNK_SHIFT_*; `GHOST_MOVE_SPEED=256`, `PLAYER_WALK_SPEED=77`, `PLAYER_JUMP_VY=110`; physics constants (`GRAVITY_DECREMENT`, `TERMINAL_VELOCITY`, `PLAYER_BBOX_HX/HY/HZ`)
- `ChunkRegistry.hpp` — Central chunk registry owning the chunks map. Provides `getChunk()` (read-only) for physics/serialization, and `generate()/activate()/deactivate()` for chunk lifecycle management. All chunk access goes through the registry; systems should use the read-only `getChunk()` when possible.
- `MessageTypes.hpp` — ServerMessageType (CHUNK_SNAPSHOT=0, CHUNK_SNAPSHOT_DELTA=2, CHUNK_TICK_DELTA=4, SELF_ENTITY=6), DeltaType, ClientMessageType (INPUT=0, JOIN=1), `InputButton` bitmask enum
- `ChunkState.hpp` — snapshot + deltas + scratch buffers; shared by Chunk and StateManager
- `GatewayInfo.hpp` — per-gateway metadata (players, watchedChunks, lastStateTick)
- `BufWriter.hpp` — sequential write helper (`write<T>` via memcpy)
- `NetworkProtocol.hpp` — serialization helpers (parseInput, parseJoin, buildSelfEntityMessage, appendToBatch). All messages use `[type(1)][size(2)]` header (3 bytes). Chunk state messages add `[chunk_id(8)][tick(4)]` = 15 byte header total. Messages are batched by direct concatenation (no length prefix).
- `VoxelTypes.hpp` — named voxel type constants (AIR=0, STONE=1, DIRT=2, GRASS=3)

**server/game/entities/**
- `PlayerEntity.hpp` — `spawn()` factory for PLAYER (PhysicsMode::FULL, grounded=false)
- `GhostPlayerEntity.hpp` — `spawn()` factory for GHOST_PLAYER (PhysicsMode::GHOST, grounded=true)
- `SheepEntity.hpp` — `spawn()` factory for SHEEP (PhysicsMode::FULL, smaller bbox, no PlayerComponent)
- `EntityFactory.hpp` — `playerFactories` map (`EntityType → PlayerSpawnFn`); pulled in by `GameEngine.hpp`

**server/game/GameEngine.hpp/cpp**
- `entt::registry registry` — single source of truth for all entity state
- `ChunkRegistry chunkRegistry` — central chunk registry (replaces raw chunks map)
- `map[GatewayId, GatewayInfo] gateways`, `map[PlayerId, entt::entity] playerEntities`
- `queuePendingPlayer()` — called on WebSocket connect; parks player in `pendingPlayers` until JOIN arrives
- `addPlayer()` — delegates to `playerFactories` map; accepts optional `EntityType` (default `GHOST_PLAYER`); used directly by tests
- `removePlayer()` — cleans chunk membership via ChunkMembershipComponent, then destroys entity
- `teleportPlayer()` — directly sets player position (for test setup / admin use)
- `tick()` → `InputSystem::apply()` → `SheepAISystem::apply()` → `stepPhysics()` → `detectChunkCrossings()` → `updateEntitiesChunks()` → `rebuildGatewayWatchedChunks()` → `serializeSnapshotDelta()` or `serializeTickDelta()`
- `detectChunkCrossings()` — Phase A: detects entities crossing chunk boundaries, marks with `PendingChunkChangeComponent`
- `updateEntitiesChunks()` — Phase B: processes pending creates/changes/deletions (entities destroyed after serialization)
- `rebuildGatewayWatchedChunks()` — Phase C: rebuilds gateway watchedChunks, activates new chunks, sends snapshots

**server/game/WorldGenerator.hpp/cpp**
- Stateless procedural terrain generator using multi-frequency simplex noise
- `generate(voxels, cx, cy, cz)` — fills voxel buffer for chunk
- `getSurfaceY(voxelX, voxelZ)` — surface height at voxel column (matches generation logic)
- `generateEntities(chunkId, registry, tick)` — spawns passive mobs (sheep) on surface grass

**server/game/Chunk.hpp/cpp**
- `set<entt::entity> entities` — chunk membership; wire ID from GlobalEntityIdComponent
- `bool activated` — true if chunk has been activated (entities spawned)
- `buildSnapshot(reg, tick)` — LZ4(voxels) zero-copy + entity section into scratch, compress if above threshold
- `buildSnapshotDelta / buildTickDelta` — staging buffer → optional LZ4 → appended to state.deltas

**server/game/ChunkRegistry.hpp**
- Central registry owning `unordered_map<ChunkId, unique_ptr<Chunk>>`
- `const Chunk* getChunk(ChunkId)` — read-only access for physics, serialization
- `Chunk* getChunkMutable(ChunkId)` — non-const access for internal modifications
- `Chunk* generate(WorldGenerator&, ChunkId, registry, tick)` — generates voxels, activates chunk (simplified: always activate on generate)
- `Chunk* activate(WorldGenerator&, ChunkId, registry, tick)` — ensures chunk exists and is activated (spawns entities)
- `bool deactivate(ChunkId, registry)` — removes non-player entities from chunk, sets activated=false
- Used by GameEngine; systems receive `const ChunkRegistry&` for read-only access

**server/game/WorldChunk.hpp/cpp**
- `voxels[65536]` — flat Y×X×Z voxel array
- `voxelsSnapshotDeltas`, `voxelsTickDeltas` — changed-voxel lists, cleared after each delta send

**server/game/components/**
- `GlobalEntityIdComponent` — `GlobalEntityId id`; assigned at spawn, never changes
- `DirtyComponent` — `snapshotDirtyFlags`, `tickDirtyFlags`; component bits (0-5) + lifecycle bits `CREATED_BIT=1<<6`, `DELETED_BIT=1<<7`; `mark(bit)`, `markCreated()`, `markDeleted()`, `clearSnapshot()`, `clearTick()`
- `DynamicPositionComponent` — x,y,z,vx,vy,vz (int32 sub-voxels), grounded, moved; `modify()`; `serializeFields(BufWriter&)`
- `EntityTypeComponent` — `EntityType type`; emplaced on every entity at creation
- `InputComponent` — `buttons` (uint8 bitmask), `yaw`, `pitch` (float radians); updated by handlePlayerInput(); read by InputSystem
- `PlayerComponent` — `PlayerId playerId`; emplaced on player entities only
- `ChunkMembershipComponent` — `currentChunkId` (assigned at spawn); managed by ChunkMembershipSystem
- `PhysicsModeComponent` — `PhysicsMode mode` (GHOST/FLYING/FULL); server-only, not serialised
- `BoundingBoxComponent` — AABB half-extents (hx, hy, hz) in sub-voxels; centered on position
- `SheepBehaviorComponent` — AI state (IDLE/WALKING), end tick, target pos, yaw; `SHEEP_BEHAVIOR_BIT=1<<1`
- `PendingCreateComponent` — `targetChunkId`; marks entity for creation in chunk (Phase B)
- `PendingChunkChangeComponent` — `newChunkId`; marks entity moving to different chunk (Phase B)
- `PendingDeleteComponent` — marks entity for deferred deletion after serialization (Phase B)

**server/game/systems/**
- `InputSystem.hpp` — `apply(registry)`: translates InputComponent (buttons+yaw+pitch) → DynamicPositionComponent velocity per EntityType; called at top of `tick()` before physics
- `PhysicsSystem.hpp` — `apply(registry, chunks)`: collision-aware physics sweeps (X/Y/Z) with voxel-context cache; handles GHOST (no collision), FLYING (collision, no gravity), FULL (collision + gravity)
- `ChunkMembershipSystem.hpp` — `detectChunkCrossings()`, `updateEntitiesChunks()`, `rebuildGatewayWatchedChunks()`, `destroyPendingDeletions()`, `markForCreation()`, `markForDeletion()`: three-phase chunk membership management. Note: SELF_ENTITY is sent once at player creation, not on every chunk change (global entity ID is stable).
- `SheepAISystem.hpp` — `apply(registry, tick)`: simple state machine (IDLE 2-5s → WALK 2s loop); sets velocity toward random target; runs before physics

**server/gateway/**
- `GatewayEngine` — uWS server; player connect/disconnect/input callbacks; `receiveGameBatch()` forwards to clients
- `StateManager` — `map[ChunkId, ChunkState]`; routes chunk state messages to watching players

**client/src/**
- `types.js` — game constants (EntityType, VoxelType, SUBVOXEL_SIZE, physics constants …)
- `utils.js` — `lz4Decompress`, `BufReader` (sequential binary read)
- `EntityRegistry.js` — **NEW** global entity registry; entities keyed by GlobalEntityId, track current chunkId; handles snapshot/delta entity parsing
- `GameClient.js` — WebSocket; parses 15-byte chunk header; owns `EntityRegistry` + chunk map; `selfEntity` finds by globalId
- `Chunk.js` — per-chunk voxel state only (entities moved to EntityRegistry); LZ4 decompression, Three.js mesh rebuild
- `components/DynamicPositionComponent.js` — mirrors server; `predictAt(tick)` for client-side interpolation
- `entities/BaseEntity.js`, `PlayerEntity.js` — now includes `chunkId` property to track current chunk
- `entities/SheepEntity.js` — procedural mesh (body + head + legs); leg swing animation when WALKING; face movement direction
- `NetworkProtocol.js` — protocol enums (ServerMessageType, DeltaType, ClientMessageType, InputButton) and serialization helpers (serializeInput, serializeJoin, parseBatch, parseHeader, parseChunkHeader, parseSelfEntity). `parseBatch` uses embedded `[type][size]` headers to split concatenated messages.
- `main.js` — Three.js scene, render loop, HUD; entity meshes keyed by GlobalEntityId only (not chunkId-entityId composite)

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
