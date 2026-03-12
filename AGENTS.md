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
- Each Chunk owns a ChunkState (unified buffer + entries + scratch); serialisation is per-chunk and parallelisable.
- 3 message levels per chunk: snapshot → snapshot delta → tick delta. See `docs/wire-format.md`.
- **Unified buffer layout**: `[snapshot][snapshot_delta][tick_delta_1][tick_delta_2]...` — snapshot deltas clear previous deltas; tick deltas append.

# Core principles

- **Pure ECS**: all entity state lives in entt components. No parallel data structures mirroring the registry.
- **Fixed-point positions**: `int32_t` sub-voxels, 1 voxel = `SUBVOXEL_SIZE` (256) units. No floats in game state.
  - `CHUNK_SHIFT_Y/X/Z = 13/13/13` → chunk coord = position >> shift (works directly on sub-voxel values).
  - Client sends button bitmask + yaw/pitch; server `InputSystem` converts to velocity each tick.
  - Rendering: divide by `SUBVOXEL_SIZE` before passing to Three.js.
- **Dirty tracking**: `DirtyComponent` carries `snapshotDirtyFlags` and `tickDirtyFlags` (1 bit per component)
  plus `snapshotDeltaType` and `tickDeltaType` (CREATE_ENTITY vs UPDATE_ENTITY). Cleared by Chunk::updateState():
  - After tick delta: clears tick flags only (snapshot flags preserved for next snapshot delta)
  - After snapshot delta or full snapshot: clears both tick and snapshot flags
  - Note: CREATE_ENTITY delta type is NOT cleared by chunk (sendSelfEntityMessages() clears it after sending SELF_ENTITY)
- **GlobalEntityId** (uint32): assigned at spawn, stable across chunk moves and server lifetime. Used on wire.
- **Serialisation ownership**: each component's `serializeFields(SafeBufWriter&)` writes its own bytes only.
  The caller (Chunk) writes the component-flags byte and decides which components to include.
- **SafeBufWriter**: auto-growing vector-based writer prevents buffer overflows. Old `BufWriter` (raw pointer) migrated for safety.
- **TEST world entity control**: `--test-entity-type <type>` is optional. If omitted, TEST mode spawns flat terrain with no entities (player-only). Use `--test-entity-type sheep` to spawn a test entity.

# Coordinate Systems

Three nested spaces (all use fixed-point integers, no floats in game state):

| Space | Type | Unit | Range | Notes |
|-------|------|------|-------|-------|
| Sub-voxel | `int32_t` | 1/256 voxel | ±2B | Physics/position storage (`SUBVOXEL_SIZE=256`) |
| Voxel | `int32_t` | 1 block | ±2B | World-space coordinates |
| Chunk | `int32_t` | 32³ voxels | Y:[-32,31], X/Z:±268M | Chunk coordinate |

**Chunk dimensions:** 32×32×32 voxels → `CHUNK_SHIFT_{Y,X,Z}=13` (log₂(32×256)).  
Chunk coord = sub-voxel position >> 13 (arithmetic shift handles negatives).

**ChunkId packing** (int64): `sint6(y) | sint29(x) | sint29(z)` — use `fromChunkPos()` / `chunkIdFromChunkPos()`.  
**VoxelIndex packing** (uint16): `uint5(y) | uint5(x) | uint5(z)` — flat array index; use `voxelIndexFromPos()`.

**Client rendering:** divide sub-voxel position by `SUBVOXEL_SIZE` (256) before passing to Three.js.

# Key types

| Type | Repr | Notes |
|------|------|-------|
| ChunkId | int64 packed | sint6(y) · sint29(x) · sint29(z); helpers: `fromChunkPos()`, `fromVoxelPos()`, `fromSubVoxelPos()` (server) / `chunkIdFrom*Pos()` (client) |
| VoxelIndex | uint16 | uint5(y) · uint5(x) · uint5(z) — flat array index; helpers: `voxelIndexFromPos()`, `getVoxelIndexPos()` |
| VoxelType | uint8 | 0 = air |
| GlobalEntityId | uint32 | assigned at spawn, stable across moves |
| PlayerId | uint32 | persistent |
| GatewayId | uint32 | |

Chunk voxels: 32 × 32 × 32 = 32 768 bytes. Use `voxelIndexFromPos(x,y,z)` to compute flat array index.

> **Note:** `ChunkEntityId` (uint16) is deprecated; `GlobalEntityId` is now used on the wire.

# Code structure

**server/common/**
- `Types.hpp` — ChunkId, VoxelIndex, VoxelType, GlobalEntityId, PlayerId, GatewayId; chunk dims; SUBVOXEL_SIZE, CHUNK_SHIFT_*; physics constants
- `EntityType.hpp` — enum PLAYER=0, GHOST_PLAYER=1, SHEEP=2
- `MessageTypes.hpp` — ServerMessageType, DeltaType, ClientMessageType, `InputButton` bitmask
- `ChunkState.hpp` — unified buffer for snapshot/delta messages; used by Chunk and GatewayEngine
- `VoxelTypes.hpp` — named voxel type constants (AIR=0, STONE=1, DIRT=2, GRASS=3)
- `SafeBufWriter.hpp` — safe sequential binary write helper (auto-growing, bounds-checked)
- `NetworkProtocol.hpp` — serialization helpers; message format: `[type(1)][size(2)]` header, chunk messages add `[chunk_id(8)][tick(4)]` = 15 bytes

**server/game/entities/**
- `{Player,GhostPlayer,Sheep}Entity.hpp` — spawn factories per entity type
- `EntityFactory.hpp` — `playerFactories` map for player spawning

**server/game/GameEngine.hpp/cpp** — main game loop; owns `registry`, `chunkRegistry`, `gateways[]`, `playerEntities[]`; tick flow: Input → AI → Physics → ChunkCrossings → UpdateChunks → RebuildWatched → Serialize

**server/game/WorldGenerator.hpp/cpp** — simplex noise terrain generator; `generate()` fills voxels, `generateEntities()` spawns sheep on grass

**server/game/Chunk.hpp/cpp** — chunk entity set + snapshot/delta builders; uses `SafeBufWriter` for entity serialization. Tracks `enteredEntities` (new arrivals) and `leftEntities` (departures) for CHUNK_CHANGE notifications.

**server/game/EntitySerializer.hpp/cpp** — entity serialization utilities; `serializeFull()` for snapshots (with `forDelta` flag for CREATE_ENTITY context), `serializeDelta()` for deltas

**server/game/ChunkRegistry.hpp** — owns `unordered_map<ChunkId, unique_ptr<Chunk>>`; provides `getChunk()` / `getChunkMutable()`, `generate()` / `activate()` / `deactivate()`

**server/game/WorldChunk.hpp/cpp** — `voxels[]` array + changed-voxel tracking for deltas

**server/game/components/**
- `GlobalEntityIdComponent` — stable uint32 ID assigned at spawn
- `DirtyComponent` — `snapshot/tickDirtyFlags` + `snapshot/tickDeltaType` (CREATE_ENTITY for new/spawned/moved, UPDATE_ENTITY default)
- `DynamicPositionComponent` — x,y,z,vx,vy,vz (int32 sub-voxels), grounded flag; `serializeFields(SafeBufWriter&)`
- `EntityTypeComponent`, `PlayerComponent`, `ChunkMembershipComponent`, `PhysicsModeComponent`, `BoundingBoxComponent`, `SheepBehaviorComponent`
- `Pending{Create,ChunkChange,Delete}Component` — deferred chunk membership changes

**server/game/systems/**
- `InputSystem.hpp` — converts InputComponent → velocity per EntityType
- `PhysicsSystem.hpp` — collision-aware sweeps (X/Y/Z); handles GHOST/FLYING/FULL modes
- `ChunkMembershipSystem.hpp` — three-phase chunk membership (detect crossings → update → rebuild watched)
- `SheepAISystem.hpp` — state machine: IDLE → WALK loop

**server/gateway/GatewayEngine.hpp** — uWS WebSocket server; handles connect/disconnect/input; manages per-chunk state cache + player metadata

**client/src/**
- `types.js`, `utils.js` — constants, ChunkId/VoxelIndex helpers, `lz4Decompress`, `BufReader`
- `EntityRegistry.js`, `Chunk.js`, `ChunkRegistry.js` — entity registry + chunk voxel storage
- `GameClient.js` — WebSocket handler; owns `EntityRegistry` + chunk map
- `components/DynamicPositionComponent.js` — client-side interpolation
- `entities/{Base,Player,Sheep}Entity.js` — entity classes
- `systems/{ChunkMembership,PhysicsPrediction}System.js` — client-side chunk tracking + interpolation
- `NetworkProtocol.js` — serialization helpers, message parsing
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
