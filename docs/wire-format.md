# voxelmmo — Wire format reference

> Keep this file up to date with every protocol change.
> It is read by AI agents: keep it concise and accurate.
>
> Implementation: `server/game/ChunkSerializer.cpp` (server), `client/src/NetworkProtocol.js` (client)

## Universal message header (all messages)

Every network message starts with a 3-byte header:

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | EntityType (message category) |
| 1 | uint16 LE | message size (total bytes including this header) |

## EntityType values

| Value | Name | Description |
|-------|------|-------------|
| 0 | CHUNK_SNAPSHOT | Full chunk state (voxels + entities) |
| 1 | CHUNK_SNAPSHOT_COMPRESSED | LZ4-compressed SNAPSHOT |
| 2 | CHUNK_SNAPSHOT_DELTA | Delta since last snapshot |
| 3 | CHUNK_SNAPSHOT_DELTA_COMPRESSED | LZ4-compressed SNAPSHOT_DELTA |
| 4 | CHUNK_TICK_DELTA | Per-tick delta |
| 5 | CHUNK_TICK_DELTA_COMPRESSED | LZ4-compressed TICK_DELTA |
| 6 | SELF_ENTITY | Player self-identification |

Odd values are the LZ4-compressed counterpart of the preceding even value.

## Chunk state messages (types 0-5)

Additional header fields after the universal header:

| Offset | Size | Field |
|--------|------|-------|
| 3 | int64 LE | ChunkId packed |
| 11 | uint32 LE | server tick when built |

Total header: 15 bytes (always uncompressed so gateway can route without decompression).

### CHUNK_SNAPSHOT / CHUNK_SNAPSHOT_COMPRESSED (type 0/1)

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

## SELF_ENTITY (type 6)

Sent once when player connects. Contains the player's global entity ID.

```
[3-byte universal header: type=6, size=13]
GlobalEntityId uint32   (player's stable entity ID)
uint32         tick     (server tick at spawn)
uint32         reserved (must be ignored by clients)
```

## DynamicPositionComponent fields (when POSITION_BIT set)

```
int32 x, y, z     sub-voxel position (server keeps always current)
int32 vx, vy, vz  sub-voxels per tick
uint8 grounded    0 = airborne, 1 = on ground
```

Total: 25 bytes. Tick is in the message header, not here.

## GlobalEntityId semantics

`GlobalEntityId` (uint32) is assigned once at entity spawn and is stable across chunk moves. IDs start from 1 (0 reserved).

## Server → Client batching

Multiple messages are concatenated directly into a single WebSocket binary frame. Each message already contains its total size in the universal header (bytes 1-2), so no additional length prefix is needed.

Batch format: `[message1][message2][message3]...` where each message starts with `[type(1)][size(2)]`.

To parse: read type (1 byte), read size (2 bytes LE), read `size-3` payload bytes, repeat.

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

## Game EntityType values (for spawn/join)

| Value | Name | Physics |
|-------|------|---------|
| 0 | PLAYER | Full: gravity + swept AABB collision |
| 1 | GHOST_PLAYER | Ghost: velocity-only, no gravity, no collision |
| 2 | SHEEP | Passive mob: wanders randomly |
