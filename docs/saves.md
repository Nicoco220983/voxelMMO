# Save System

This document describes the voxel world save system implementation.

## Overview

The save system provides persistent storage for voxel data, ensuring player modifications survive server restarts. The system uses a hybrid approach:

- **Global state** (JSON): Seed, generator type, metadata
- **Chunk data** (binary): Individual chunk voxel data with LZ4 compression

## Directory Structure

```
saves/<gameKey>/
├── global.json          # Game configuration and metadata
└── chunks/
    └── <packed_chunk_id>.chunk   # Binary voxel data
```

## File Formats

### Global State (global.json)

```json
{
  "gameKey": "voxelmmo_default",
  "version": 1,
  "seed": 123456789,
  "generatorType": "NORMAL",
  "createdAt": "2026-03-28T18:29:46Z",
  "lastSavedAt": "2026-03-28T20:15:30Z"
}
```

### Chunk File (.chunk)

Binary format:
```
[4 bytes]  Magic "VMMO" (0x4F4D4D56)
[4 bytes]  Format version (uint32 = 1)
[8 bytes]  ChunkId packed value
[4 bytes]  Flags (bit 0: compressed)
[4 bytes]  Uncompressed data size (32768)
[4 bytes]  Stored data size
[N bytes]  Voxel data (raw or LZ4 compressed)
```

## Chunk Lifecycle

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  CHUNK LIFECYCLE                                                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. GENERATE (on demand)                                                    │
│     ├─> Player approaches spawn area                                        │
│     ├─> Check save file exists?                                             │
│     │   ├─> YES → Load voxels from disk                                     │
│     │   └─> NO  → Generate procedurally                                     │
│     └─> SAVE TO DISK immediately (if newly generated)                       │
│                                                                             │
│  2. ACTIVE (entities spawned)                                               │
│     └─> Entities exist in chunk, physics runs                               │
│                                                                             │
│  3. WATCHED (players nearby)                                                │
│     └─> Chunk included in player snapshots                                  │
│                                                                             │
│  4. UNWATCHED → SAVE & UNLOAD                                               │
│     ├─> watchingPlayers becomes empty                                       │
│     ├─> Deactivate (remove entities)                                        │
│     ├─> Save voxels to disk                                                 │
│     └─> Unload from registry (free memory)                                  │
│                                                                             │
│  5. SHUTDOWN                                                                │
│     └─> Save only ACTIVE chunks (unwatched already saved)                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Key Behaviors

### Load from Save First

When a chunk needs to be generated:
1. Check if chunk file exists in saves
2. If yes: Load voxels from save (skip generation)
3. If no: Generate procedurally

This ensures player modifications are preserved across server restarts.

### Save on Generation

Newly generated chunks (not loaded from save) are saved immediately. This ensures:
- No data loss if server crashes
- Deterministic world from seed is preserved

### Unload Unwatched Chunks

Every tick, chunks with no watching players are:
1. Saved to disk
2. Deactivated (entities removed)
3. Unloaded from memory

This prevents unbounded memory growth as players explore.

### Shutdown - Signal Handler Saves

On graceful shutdown (SIGINT/SIGTERM), the signal handler directly calls `saveActiveChunks()` to save all active chunks. Unwatched chunks were already saved when unloaded. The game loop runs in a separate thread and does not perform the final save.

## Signal Handling

| Signal | Action |
|--------|--------|
| SIGINT (Ctrl+C) | Graceful shutdown: save active chunks, exit |
| Second SIGINT | Force quit: immediate exit, no save |
| SIGTERM | Graceful shutdown: save active chunks, exit |

**Note:** The signal handler directly calls `saveActiveChunks()` and exits. The game loop runs in a separate thread and is terminated on shutdown. This design avoids threading issues with the uWebSockets event loop.

## Implementation Details

### SaveSystem Class

```cpp
class SaveSystem {
    // Load/create global state
    GlobalState loadOrCreateGlobalState(uint32_t cliSeed, GeneratorType cliType);
    
    // Save/load individual chunks
    bool saveChunkVoxels(ChunkId id, const std::vector<VoxelType>& voxels);
    bool loadChunkVoxels(ChunkId id, std::vector<VoxelType>& outVoxels);
    bool hasSavedChunk(ChunkId id);
    
    // Bulk save operations
    size_t saveAllChunks(const ChunkRegistry& registry);
    size_t saveActiveChunks(const ChunkRegistry& registry);
};
```

### ChunkRegistry Methods

```cpp
class ChunkRegistry {
    // Generate or load chunk
    Chunk* generate(WorldGenerator& gen, ChunkId id, SaveSystem* save);
    
    // Activate/deactivate entity spawning
    Chunk* activate(ChunkId id, ...);
    bool deactivate(ChunkId id, entt::registry& registry);
    
    // Unload from memory (save first!)
    bool unload(ChunkId id);
    
    // Check if chunk is active
    bool isActive(ChunkId id) const;
};
```

### ChunkMembershipSystem

```cpp
// Called every tick
auto result = ChunkMembershipSystem::update(...);

// Unload unwatched chunks (call after update)
ChunkMembershipSystem::unloadUnwatchedChunks(registry, saveSystem);
```

## Performance Considerations

- **LZ4 compression**: Chunks are compressed when beneficial (>50% reduction)
- **Lazy loading**: Chunks only loaded when needed
- **Unload on unwatched**: Memory freed when players leave area
- **Atomic writes**: Chunks written to temp file, then renamed

## Future Improvements

- [ ] Chunk dirty tracking (only save modified chunks)
- [ ] Async save I/O (don't block game thread)
- [ ] Chunk versioning for migrations
- [ ] Incremental backup support
