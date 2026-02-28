# voxelmmo

A high performance online webgame massive multiplayer, on wide generated world.

# Stack

- Server: C++20, entt ECS, uWebSockets, LZ4 — built with CMake + vcpkg + Ninja
- Client: vanilla JS (JSDoc typed, `// @ts-check`), Three.js, lz4js — bundled with Vite
- nginx: static files + WebSocket proxy to gateway port 8080

# High level architecture

- Authoritative server using entt C++ ECS
- Game logic / player communication separation: GameEngine vs GatewayEngine (may run on separate servers, communicate via Unix socket or TCP)
- Game is entirely chunked. Chunks activated/deactivated when a player comes close. Dynamically generated at first activation.
- Each Chunk owns a ChunkState (snapshot + deltas + scratch). Serialisation is per-chunk and independent, ready for parallel execution.
- Snapshot: voxels LZ4-compressed zero-copy from world.voxels. Entity section serialised to scratch then copied or compressed into snapshot.
- Deltas: raw payload written directly into ChunkState, then compressed in-place via scratch if above threshold.
- ChunkState is shared between game side (Chunk::state) and gateway side (StateManager) — same structure, same wire format.
- Client: Three.js with naive voxel meshing, client-side LZ4 decompression via lz4js.
- 3 levels of chunk state message: snapshot, snapshot delta, tick delta.

# Code structure

- dir server/
  - file main.cpp # runs GameEngine thread + GatewayEngine (uWS) main thread
  - dir common/
    - file Types.hpp # ChunkId, VoxelId, VoxelType, EntityId, PlayerId, GatewayId, chunk dimensions
    - file MessageTypes.hpp # ChunkMessageType, DeltaType, EntityType enums
    - file ChunkState.hpp # shared snapshot/delta cache used by Chunk and StateManager
    - file VoxelTypes.hpp
  - dir game/
    - file GameEngine.hpp/cpp
      - class GameEngine
        - map[GatewayId, GatewayInfo{set[PlayerId], set[ChunkId]}] gateways
        - map[ChunkId, Chunk] chunks
        - checkPlayersChunks() # populates watchedChunks + activates chunks
        - serializeSnapshot(GatewayId) # calls chunk.buildSnapshot(), dispatches
        - serializeSnapshotDelta() # builds each chunk once, dispatches to all gateways
        - serializeTickDelta() # same pattern
    - file Chunk.hpp/cpp
      - ChunkId id
      - set[PlayerId] presentPlayers, watchingPlayers
      - WorldChunk world
      - set[EntityId] entities
      - ChunkState state # snapshot + snapshotDelta + tickDeltas + scratch
      - buildSnapshot() # LZ4(voxels) zero-copy + entity section copy/compress
      - buildSnapshotDelta() # raw → ChunkState, then maybe compress in-place
      - buildTickDelta() # same
    - file WorldChunk.hpp/cpp
      - vector[VoxelType] voxels
      - vector[[VoxelId,VoxelType]] voxelsSnapshotDeltas, voxelsTickDeltas
      - generate(), modifyVoxels(), serializeSnapshot/Delta/TickDelta(buf)
    - dir entities/
      - file BaseEntity.hpp    # serializeSnapshot(buf,off), serializeDelta(buf,off,mask)
      - file PlayerEntity.hpp  # extends BaseEntity, adds ChunkId currentChunk
    - dir components/
      - file DirtyComponent.hpp     # snapshotDirtyFlags, tickDirtyFlags (1 bit/component)
      - file DynamicPositionComponent.hpp  # POSITION_BIT, x/y/z + vx/vy/vz with modify()
  - dir gateway/
    - file GatewayEngine.hpp/cpp # uWS server, stores uwsLoop ptr for cross-thread defer
    - file StateManager.hpp/cpp  # map[ChunkId, ChunkState], routes received messages
- dir client/
  - file index.html
  - file jsconfig.json, vite.config.js, package.json
  - dir src/
    - file types.js      # JSDoc typedefs + enums + constants (mirrors server types)
    - file GameClient.js # WebSocket wrapper, parses 9-byte header, dispatches
    - file ChunkManager.js # voxel state + LZ4 decompression + Three.js mesh rebuild
    - file main.js       # Three.js scene, render loop, HUD
- dir nginx/
  - file nginx.conf
- dir scripts/
  - file install.sh, build.sh, start.sh

# Types

- ChunkId: sint6(y) × sint29(x) × sint29(z) packed into int64
- VoxelId: uint4(y) × uint6(x) × uint6(z) packed into uint16
- VoxelType: uint8 — EntityId: uint16 — PlayerId: uint32
- Chunk voxels: 64×16×64 = 65 536 bytes

# Chunk State message wire format

Snapshot (always SNAPSHOT_COMPRESSED):
- uint8:  ChunkMessageType = SNAPSHOT_COMPRESSED
- int64:  ChunkId
- uint8:  flags (bit 0 = entity section LZ4 compressed)
- int32:  compressed_voxel_size; then LZ4(voxels[65536])
- int32:  entity_section_stored_size
  - if flags&1: int32 entity_uncompressed_size + LZ4(entity_data)
  - else: raw entity_data (int32 count + records)

Delta (SNAPSHOT_DELTA / TICK_DELTA, optionally _COMPRESSED):
- uint8:  ChunkMessageType
- int64:  ChunkId
- if compressed: int32 uncompressed_payload_size + LZ4(payload)
- else payload: int32 voxel_count + [(VoxelId uint16, VoxelType)]
                int32 entity_count + [(DeltaType, EntityId, EntityType, ComponentFlags, ComponentStates...)]
