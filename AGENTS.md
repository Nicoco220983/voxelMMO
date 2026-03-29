# voxelmmo

A high performance online webgame massive multiplayer, on wide generated world.

# Stack

- Server: C++20, entt ECS, uWebSockets, LZ4 — built with CMake + vcpkg + Ninja
- Client: vanilla JS (JSDoc typed, `// @ts-check`), Three.js, lz4js — bundled with Vite
- nginx: static files + WebSocket proxy to gateway port 8080

# Architecture

- Authoritative server with entt C++ ECS. No game logic in the gateway.
- GameEngine (game loop) and GatewayEngine (uWS) are decoupled; can run on separate servers.
- World is fully chunked. Chunks activate on first player approach, generate terrain on activation. Chunks unload when unwatched to free memory.
- GameEngine serializes chunks into a single concatenated buffer (ChunkSerializer.chunkBuf) per tick, dispatched to all gateways.
- 3 message levels per chunk: snapshot → snapshot delta → tick delta. See `docs/wire-format.md`.
- **Gateway-side ChunkState**: Each gateway maintains per-chunk unified buffers (`[snapshot][snapshot_delta][tick_delta_1]...`) for late-joining players.

# Core principles

- **Pure ECS**: all entity state lives in entt components. No parallel data structures mirroring the registry.
- **Fixed-point positions**: `int32_t` sub-voxels, 1 voxel = `SUBVOXEL_SIZE` (256) units. No floats in game state.
  - `CHUNK_SHIFT_Y/X/Z = 13/13/13` → chunk coord = position >> shift (works directly on sub-voxel values).
  - Client sends button bitmask + yaw/pitch; server `InputSystem` converts to velocity each tick.
  - Rendering: divide by `SUBVOXEL_SIZE` before passing to Three.js.
- **Dirty tracking**: `DirtyComponent` carries `dirtyFlags` (1 bit per component), `deltaType` (CREATE_ENTITY vs UPDATE_ENTITY),
  and `snapshotDeltaType` (for SELF_ENTITY detection). Cleared after each tick:
  - `dirtyFlags` and `deltaType` cleared after every tick delta
  - `snapshotDeltaType` preserved until snapshot delta (for SELF_ENTITY detection)
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
- `NetworkProtocol.hpp` — ServerMessageType, DeltaType, ClientMessageType, `InputButton`/`InputType` enums; message format: `[type(1)][size(2)]` header, chunk messages add `[chunk_id(8)][tick(4)]` = 15 bytes
- `ChunkSerializer.hpp` — per-tick serialization buffers (chunkBuf, selfEntityBuf, scratch); used by GameEngine
- `gateway/ChunkState.hpp` — gateway-side per-chunk unified buffer for caching state
- `VoxelTypes.hpp` — named voxel type constants (AIR=0, STONE=1, DIRT=2, GRASS=3)
- `SafeBufWriter.hpp` — safe sequential binary write helper (auto-growing, bounds-checked)

**server/game/entities/**
- `{Player,GhostPlayer,Sheep}Entity.hpp` — spawn factories per entity type
- `EntityFactory.hpp` — `playerFactories` map for player spawning

**server/game/GameEngine.hpp/cpp** — main game loop; owns `registry`, `chunkRegistry`, `gateways[]`, `playerEntities[]`, `ser` (ChunkSerializer); tick flow: Input → AI → Physics → ChunkCrossings → UpdateChunks → RebuildWatched → Serialize → Dispatch. Provides `run()` to start the game loop in a separate thread and `stop()` for graceful shutdown.

**server/game/WorldGenerator.hpp/cpp** — simplex noise terrain generator; `generate()` fills voxels, `generateEntities()` spawns sheep on grass

**server/game/Chunk.hpp** — chunk entity set; delegates serialization to ChunkSerializer

**server/game/ChunkSerializer.hpp/cpp** — serialization orchestration; builds all chunk messages into per-tick buffers.
- `serializeChunks()` — builds SNAPSHOT, SNAPSHOT_DELTA, TICK_DELTA for all chunks
- `chunkBuf` — concatenated chunk messages; `selfEntityBuf` — SELF_ENTITY messages
- Voxel serialization: `voxels[]` LZ4-compressed; deltas as (VoxelIndex, VoxelType) pairs

**server/game/EntitySerializer.hpp/cpp** — entity serialization utilities; delegates to type-specific serializers.
- `serializeFull()` — full entity state for snapshots/snapshot deltas
- `serializeDelta()` — delta entity state for tick deltas

**server/game/EntityTypeSerializers.hpp/cpp** — per-entity-type component serialization.
- Each entity type defines `serialize*Full()` and `serialize*Delta()` methods
- Player/GhostPlayer: position + player-specific data; Sheep: position + behavior data

**server/game/ChunkRegistry.hpp** — owns `unordered_map<ChunkId, unique_ptr<Chunk>>`; provides `getChunk()` / `getChunkMutable()`, `generate()` / `activate()` / `deactivate()` / `unload()`

**server/game/WorldChunk.hpp/cpp** — `voxels[]` array + `voxelsDeltas` for per-tick voxel changes

**server/game/components/**
- `GlobalEntityIdComponent` — stable uint32 ID assigned at spawn
- `DirtyComponent` — `dirtyFlags` (component change bits) + `deltaType` (CREATE_ENTITY/UPDATE_ENTITY/DELETE_ENTITY) + `snapshotDeltaType` (for SELF_ENTITY detection)
- `DynamicPositionComponent` — x,y,z,vx,vy,vz (int32 sub-voxels), grounded flag; `serializeFields(SafeBufWriter&)`
- `EntityTypeComponent`, `PlayerComponent`, `ChunkMembershipComponent`, `PhysicsModeComponent`, `BoundingBoxComponent`, `SheepBehaviorComponent`
- `Pending{Create,ChunkChange,Delete}Component` — deferred chunk membership changes
- `DisconnectedPlayerComponent` — marks players for cleanup; processed by `DisconnectedPlayerSystem`

**server/game/systems/**
- `InputSystem.hpp` — converts InputComponent → velocity per EntityType; handles `InputType::MOVE`
- `PhysicsSystem.hpp` — collision-aware sweeps (X/Y/Z); handles GHOST/FLYING/FULL modes
- `ChunkMembershipSystem.hpp` — three-phase chunk membership (detect crossings → update → activate chunks near players); also unloads unwatched chunks
- `DisconnectedPlayerSystem.hpp` — cleans up entities marked with `DisconnectedPlayerComponent`
- `SheepAISystem.hpp` — state machine: IDLE → WALK loop

**server/game/SaveSystem.hpp/cpp** — persistent storage for voxel data:
- `global.yaml` — seed, generator type, timestamps (key: value format)
- `chunks/*.chunk` — LZ4-compressed voxel data (one file per chunk)
- Chunks saved: on generation, on unload (when unwatched), and on shutdown (active chunks only)
- Chunks loaded from save when available (preserves player modifications)

**server/gateway/GatewayEngine.hpp** — uWS WebSocket server; handles connect/disconnect/input; manages per-chunk state cache + player metadata. Runs in main thread with blocking `listen()`.

**client/src/**
- `types.js`, `utils.js` — constants, ChunkId/VoxelIndex helpers, `lz4Decompress`, `BufReader`
- `EntityRegistry.js`, `Chunk.js`, `ChunkRegistry.js` — entity registry + chunk voxel storage
- `GameClient.js` — WebSocket handler; owns `EntityRegistry` + chunk map
- `components/DynamicPositionComponent.js` — client-side interpolation
- `entities/{Base,Player,Sheep}Entity.js` — entity classes
- `systems/{ChunkMembership,PhysicsPrediction}System.js` — client-side chunk tracking + interpolation
- `systems/VoxelHighlightSystem.js` — voxel selection highlight + deletion (click to remove voxels)
- `NetworkProtocol.js` — serialization helpers, message parsing
- `main.js` — Three.js scene, render loop, HUD
- `VoxelTextures.js` — runtime texture atlas builder; loads PNGs from `client/static/assets/voxels/` into a `THREE.CanvasTexture` used by chunk meshing

**docs/**
- `wire-format.md` — chunk message binary layout (keep in sync with ChunkSerializer.cpp)
- `saves.md` — save system documentation (chunk lifecycle, file formats)

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
