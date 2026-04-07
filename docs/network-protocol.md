# voxelmmo — Network Protocol

> Complete documentation of the server-to-client message protocol, state management, and serialization hierarchy.
> 
> Implementation:
> - `server/game/ChunkSerializer.cpp` (message generation)
> - `server/game/EntitySerializer.cpp` (entity serialization)
> - `server/gateway/GatewayEngine.cpp` (state caching & broadcast)
> - `server/gateway/ChunkState.hpp` (per-chunk state history)
> - `client/src/NetworkProtocol.js` (client parsing)

---

## Architecture Overview

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│   GameEngine    │────▶│  GatewayEngine   │────▶│     Clients     │
│  (game thread)  │     │  (main thread)   │     │  (WebSocket)    │
└─────────────────┘     └──────────────────┘     └─────────────────┘
         │                       │
    Generates              Maintains per-chunk
    state messages         unified buffer cache
    each tick              for late-joining players
```

### Message Flow

1. **GameEngine** generates state messages every tick:
   - `SNAPSHOT`: Full chunk state (first time chunk is serialized)
   - `SNAPSHOT_DELTA`: Periodic full-state sync (every 20 ticks)
   - `TICK_DELTA`: Incremental changes between snapshots

2. **GatewayEngine** receives messages and:
   - Updates per-chunk `ChunkState` (unified buffer with history)
   - Broadcasts to all connected clients
   - Sends full cached state to newly connected players

3. **ChunkState** maintains a unified buffer:
   ```
   [snapshot][snapshot_delta_1][tick_delta_1][tick_delta_2]...
   ```

---

## State Message Hierarchy

There are **three levels** of state messages, ordered by decreasing payload size and increasing frequency:

| Level | Message Type | Frequency | Content |
|-------|--------------|-----------|---------|
| 1 | `CHUNK_SNAPSHOT` | Once per chunk activation | Full voxels (LZ4) + full entities |
| 2 | `CHUNK_SNAPSHOT_DELTA` | Every 20 ticks | Full voxels (LZ4) + full entities |
| 3 | `CHUNK_TICK_DELTA` | Every tick | Voxel deltas + entity deltas |

### Override Semantics

**Snapshots override all previous state.** When a client receives:
- `SNAPSHOT`: Discard all previous chunk data, use this as authoritative baseline
- `SNAPSHOT_DELTA`: Discard all previous deltas (keep snapshot baseline), apply as new baseline
- `TICK_DELTA`: Apply incrementally to current state

This design ensures late-joining players always receive a complete picture by receiving the cached `[snapshot][deltas...]` sequence.

---

## Universal Message Header (all messages)

Every network message starts with a 3-byte header:

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | EntityType (message category) |
| 1 | uint16 LE | message size (total bytes including this header) |

## ServerMessageType values

| Value | Name | Description |
|-------|------|-------------|
| 0 | `CHUNK_SNAPSHOT` | Full chunk state (voxels + entities) |
| 1 | `CHUNK_SNAPSHOT_COMPRESSED` | LZ4-compressed SNAPSHOT |
| 2 | `CHUNK_SNAPSHOT_DELTA` | Delta since last snapshot |
| 3 | `CHUNK_SNAPSHOT_DELTA_COMPRESSED` | LZ4-compressed SNAPSHOT_DELTA |
| 4 | `CHUNK_TICK_DELTA` | Per-tick delta |
| 5 | `CHUNK_TICK_DELTA_COMPRESSED` | LZ4-compressed TICK_DELTA |
| 6 | `SELF_ENTITY` | Player self-identification |

Odd values are the LZ4-compressed counterpart of the preceding even value.

---

## Chunk State Messages (types 0-5)

Additional header fields after the universal header:

| Offset | Size | Field |
|--------|------|-------|
| 3 | int64 LE | ChunkId packed |
| 11 | uint32 LE | server tick when built |

Total header: 15 bytes (always uncompressed so gateway can route without decompression).

### CHUNK_SNAPSHOT / CHUNK_SNAPSHOT_COMPRESSED (type 0/1)

First message sent when a chunk is activated. Contains complete state.

```
[3-byte universal header]
[12-byte chunk header: ChunkId(8) + tick(4)]
uint8   flags           (bit 0 = entity section is LZ4-compressed)
int32   compressed_voxel_size
bytes   LZ4(voxels[32768])
int32   entity_section_stored_size
  if flags & 1:
    int32   entity_uncompressed_size
    bytes   LZ4(entity_data)
  else:
    bytes   entity_data   → int32 count + entity records
```

#### Entity record (snapshot)

```
GlobalEntityId uint32    (stable across chunk moves and server lifetime)
EntityType     uint8     (entity's game type: PLAYER, GHOST_PLAYER, SHEEP)
ComponentFlags uint8     (bitmask of present components)
  if POSITION_BIT:  DynamicPositionComponent fields (see below)
  if SHEEP_BEHAVIOR_BIT: SheepBehaviorComponent fields
```

### CHUNK_SNAPSHOT_DELTA (types 2-3, optionally _COMPRESSED)

Sent periodically (every 20 ticks) after initial snapshot. Contains voxel deltas but **full entities** (not deltas).

```
[3-byte universal header]
[12-byte chunk header: ChunkId(8) + tick(4)]
uint8   flags           (bit 0 = entity section is LZ4-compressed)
int32   voxel_count
repeat: VoxelIndex uint16 (uint5 y | uint5 x | uint5 z) · VoxelType uint8
int32   entity_section_stored_size
  if flags & 1:
    int32   entity_uncompressed_size
    bytes   LZ4(entity_data)
  else:
    bytes   entity_data   → int32 count + entity records (same format as SNAPSHOT)
```

**Entity record format:** Same as CHUNK_SNAPSHOT (full entity state, no DeltaType prefix).

### CHUNK_TICK_DELTA (types 4-5, optionally _COMPRESSED)

Sent every tick between snapshot deltas. Contains voxel deltas and **entity deltas**.

```
[3-byte universal header]
[12-byte chunk header: ChunkId(8) + tick(4)]
  if _COMPRESSED:
    int32   uncompressed_payload_size
    bytes   LZ4(payload)
  else payload:
    int32   voxel_count
    repeat: VoxelIndex uint16 (uint5 y | uint5 x | uint5 z) · VoxelType uint8
    int32   entity_count
    repeat: entity delta record
```

#### Entity delta record (TICK_DELTA only)

```
DeltaType      uint8   (enum: 0=CREATE, 1=UPDATE, 2=DELETE, 3=CHUNK_CHANGE)
GlobalEntityId uint32  (stable across chunk moves and server lifetime)
  if DeltaType is CREATE or UPDATE:
    EntityType     uint8     (entity's game type)
    ComponentFlags uint8     (bitmask of dirty components, bits 0-5)
      if POSITION_BIT:       DynamicPositionComponent fields (see below)
      if SHEEP_BEHAVIOR_BIT: SheepBehaviorComponent fields
  if DeltaType is CHUNK_CHANGE:
    NewChunkId     int64 LE (packed) - the chunk the entity moved to
  if DeltaType is DELETE:
    (no additional fields)
```

**DeltaType semantics (exclusive values):**
- **CREATE_ENTITY (0)**: Entity appears in this chunk for the first time.
- **UPDATE_ENTITY (1)**: Entity already known; only dirty components present.
- **DELETE_ENTITY (2)**: Entity removed from this chunk.
- **CHUNK_CHANGE_ENTITY (3)**: Entity moved to a different chunk (old chunk sends this).

---

## SELF_ENTITY (type 6)

Sent once when player connects. Contains the player's global entity ID.

```
[3-byte universal header: type=6, size=13]
GlobalEntityId uint32   (player's stable entity ID)
uint32         tick     (server tick at spawn)
uint32         reserved (must be ignored by clients)
```

---

## Gateway State Caching

### ChunkState Unified Buffer

Each gateway maintains a `ChunkState` for every active chunk:

```
Layout: [snapshot][snapshot_delta_1][tick_delta_1][tick_delta_2]...
         ▲                              ▲
         │                              └── entries[2..N]
         └── entries[0]
```

The `entries` vector tracks `{tick, offset, length}` for each message.

### Receive Logic

When the gateway receives messages from the game engine:

| Received | Action |
|----------|--------|
| `SNAPSHOT` | Clear buffer, store as new snapshot (entries = [{tick, 0, size}]) |
| `SNAPSHOT_DELTA` | Keep snapshot entry, clear all deltas, append delta |
| `TICK_DELTA` | Append to existing buffer |

### Late-Joiner Handling

When a new player connects, the gateway sends all cached chunk states:

```cpp
for (const auto& [cid, state] : chunkStates) {
    auto [stateData, length] = state.getDataToSend(0);  // 0 = new player
    if (length > 0) sendToPlayer(pid, stateData, length);
}
```

The `getDataToSend(lastReceivedTick)` method:
- Returns entire buffer if `lastReceivedTick == 0` (new watcher)
- Returns data from first entry with `tick > lastReceivedTick` (catch-up)
- Returns `{nullptr, 0}` if up-to-date

---

## DynamicPositionComponent fields (when POSITION_BIT set)

```
int32 x, y, z     sub-voxel position (server keeps always current)
int32 vx, vy, vz  sub-voxels per tick
uint8 grounded    0 = airborne, 1 = on ground
```

Total: 25 bytes. Tick is in the message header, not here.

---

## GlobalEntityId semantics

`GlobalEntityId` (uint32) is assigned once at entity spawn and is stable across chunk moves. IDs start from 1 (0 reserved).

---

## Server → Client batching

Multiple messages are concatenated directly into a single WebSocket binary frame. Each message already contains its total size in the universal header (bytes 1-2), so no additional length prefix is needed.

Batch format: `[message1][message2][message3]...` where each message starts with `[type(1)][size(2)]`.

To parse: read type (1 byte), read size (2 bytes LE), read `size-3` payload bytes, repeat.

---

## Client → Server (binary WebSocket frames)

All frames use the universal header `[type][size]`.

### INPUT (type = 0x00) — 14 bytes total

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | type = 0x00 (EntityType for INPUT messages) |
| 1 | uint16 LE | size = 14 |
| 3 | uint8 | InputType (0 = MOVE) |
| 4 | uint8 | InputButton bitmask |
| 5 | float32 LE | yaw (radians) |
| 9 | float32 LE | pitch (radians) |

#### InputButton bitmask

| Bit | Name | PLAYER effect | GHOST_PLAYER effect |
|-----|------|---------------|---------------------|
| 0 | FORWARD | Move forward | Move forward (3D for ghost) |
| 1 | BACKWARD | Move backward | Move backward |
| 2 | LEFT | Strafe left | Strafe left |
| 3 | RIGHT | Strafe right | Strafe right |
| 4 | JUMP | Jump when grounded | Ascend |
| 5 | DESCEND | (ignored) | Descend |

### JOIN (type = 0x01) — 21 bytes total

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | type = 0x01 |
| 1 | uint16 LE | size = 21 |
| 3 | uint8 | EntityType (0=PLAYER, 1=GHOST_PLAYER) |
| 4 | 16 bytes | Session token (UUID for entity recovery) |

Must be the first message after WebSocket connection. The session token allows the server to recover the player's entity after a reconnect (e.g., page refresh). A zeroed token indicates no previous session.

### VOXEL_DELETION (type = 0x02) — 17 bytes total

Sent when player clicks to delete a voxel.

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | type = 0x02 |
| 1 | uint16 LE | size = 17 |
| 3 | int32 LE | voxel X (world coordinates) |
| 7 | int32 LE | voxel Y |
| 11 | int32 LE | voxel Z |

Server validates the request and broadcasts voxel changes to all clients in the affected chunk.

---

## Game EntityType values (for spawn/join)

| Value | Name | Physics |
|-------|------|---------|
| 0 | PLAYER | Full: gravity + swept AABB collision |
| 1 | GHOST_PLAYER | Ghost: velocity-only, no gravity, no collision |
| 2 | SHEEP | Passive mob: wanders randomly |

---

## Client-Side Tick Protection

The client implements a **two-level tick protection system** to handle out-of-order message arrival, which can occur when:
- A new player receives cached chunk states (different chunks have different message histories)
- Network reordering causes messages to arrive out of sequence
- Messages from different chunks reference the same entity (cross-chunk moves)

### Two-Level Protection

```
┌─────────────────────────────────────────────────────────────┐
│  ENTITY LEVEL: lastCreateReceivedTick                       │
│  - Set when CREATE_ENTITY is processed                      │
│  - Protects against stale DELETE and CHUNK_CHANGE           │
│  - Protects against stale CREATE (re-create attempts)       │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  COMPONENT LEVEL: lastUpdateReceivedTick (per component)    │
│  - Set when component is updated (CREATE or UPDATE)         │
│  - Protects against stale partial component updates         │
│  - Missing components in CREATE = default values            │
└─────────────────────────────────────────────────────────────┘
```

### Validation Rules

| Operation | Validation | Effect on Entity | Effect on Components |
|-----------|------------|------------------|----------------------|
| **SNAPSHOT** @ T | Always processed | `lastCreateReceivedTick = T` | All set to T |
| **SNAPSHOT_DELTA** @ T | Always processed | `lastCreateReceivedTick = T` | All set to T |
| **CREATE_ENTITY** @ T | Reject if `T < lastCreateReceivedTick` | `lastCreateReceivedTick = T` | All set to T |
| **UPDATE_ENTITY** @ T | Reject if `T < lastCreateReceivedTick` | None | Present comps: set to T if `T >= comp.lastUpdateTick` |
| **CHUNK_CHANGE** @ T | Ignore if `T < lastCreateReceivedTick` | Chunk membership updated | None |
| **DELETE_ENTITY** @ T | Ignore if `T < lastCreateReceivedTick` | Entity deleted | N/A |

### Key Design Decisions

**1. CREATE_ENTITY resets all component ticks**

When a CREATE_ENTITY is received and accepted:
- `entity.lastCreateReceivedTick = messageTick`
- For **every** component: `component.lastUpdateReceivedTick = messageTick`
- Components present in message: deserialized normally
- Components **missing** from message: remain at default values, but marked as "updated at create tick"

This ensures that missing components = default values, and future stale updates to those components are correctly rejected.

**2. Cross-chunk entity moves**

When entity moves Chunk A → Chunk B at tick 100:
- Server sends `CHUNK_CHANGE` (A→B) to Chunk A's delta
- Server sends `CREATE_ENTITY` to Chunk B's delta
- Client may receive these in any order

Protection ensures:
- If Chunk B CREATE arrives first: Entity created with `lastCreateReceivedTick = 100`
- If Chunk A CHUNK_CHANGE arrives later: Check `100 >= 100` ✓ processed (idempotent)
- If stale CHUNK_CHANGE from tick 50 arrives: Check `50 < 100` ✗ ignored

**3. Stale message handling**

Stale messages are silently discarded (with debug logging). The client continues processing newer messages. This provides **eventual consistency**: if a stale message is discarded, the entity state remains correct because a newer message has already been processed.

### Implementation Notes

**EntityRegistry** (`client/src/EntityRegistry.js`):
- Validates ticks before creating/updating/deleting entities
- Skips component bytes when messages are rejected (maintains reader position)

**BaseEntity** (`client/src/entities/BaseEntity.js`):
- Tracks `lastCreateReceivedTick`
- `onEntityCreated(createTick)` resets all component update ticks
- `deserializeComponents()` handles CREATE vs UPDATE semantics

**Components** (e.g., `DynamicPositionComponent.js`):
- Each component tracks `lastUpdateReceivedTick`
- `deserializeWithTick()` validates tick before applying update
- `onEntityCreated()` sets the tick to create tick (for default values)

**BufReader** (`client/src/utils.js`):
- `skip(n)` method for skipping bytes without reading (used when rejecting stale updates)
