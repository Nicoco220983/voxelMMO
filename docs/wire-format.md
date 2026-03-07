# voxelmmo — Wire format reference

> Keep this file up to date with every protocol change.
> It is read by AI agents: keep it concise and accurate.

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

### CHUNK_SNAPSHOT_DELTA / CHUNK_TICK_DELTA (types 2-5, optionally _COMPRESSED)

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

#### Entity delta record

```
DeltaType      uint8   (0=CREATE, 1=UPDATE, 2=DELETE, 3=CHUNK_CHANGE)
GlobalEntityId uint32  (stable across chunk moves and server lifetime)
  if DeltaType == CREATE or UPDATE:
    EntityType     uint8     (entity's game type)
    ComponentFlags uint8     (bitmask of dirty components, bits 0-5)
      if POSITION_BIT:       DynamicPositionComponent fields (see below)
      if SHEEP_BEHAVIOR_BIT: SheepBehaviorComponent fields
  if DeltaType == CHUNK_CHANGE:
    NewChunkId     int64 LE (packed) - the chunk the entity moved to
  if DeltaType == DELETE:
    (no additional fields)
```

**DeltaType semantics:**
- **CREATE_ENTITY (0)**: Entity appears in this chunk for the first time.
- **UPDATE_ENTITY (1)**: Entity already known; only dirty components present.
- **DELETE_ENTITY (2)**: Entity removed from this chunk.
- **CHUNK_CHANGE_ENTITY (3)**: Entity moved to a different chunk (old chunk sends this).

## SELF_ENTITY (type 6)

Sent once when player connects. Contains the player's global entity ID.

```
[3-byte universal header: type=6, size=21]
GlobalEntityId uint32   (player's stable entity ID)
int64          ChunkId  (packed, player's initial chunk)
uint32         tick     (server tick at spawn)
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

## Client → Server (binary WebSocket frames)

All frames use the universal header `[type][size]`.

### INPUT (type = 0x00) — 13 bytes total

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | type = 0x00 (EntityType for INPUT messages) |
| 1 | uint16 LE | size = 13 |
| 3 | uint8 | InputButton bitmask |
| 4 | float32 LE | yaw (radians) |
| 8 | float32 LE | pitch (radians) |

#### InputButton bitmask

| Bit | Name | PLAYER effect | GHOST_PLAYER effect |
|-----|------|---------------|---------------------|
| 0 | FORWARD | Move forward | Move forward (3D for ghost) |
| 1 | BACKWARD | Move backward | Move backward |
| 2 | LEFT | Strafe left | Strafe left |
| 3 | RIGHT | Strafe right | Strafe right |
| 4 | JUMP | Jump when grounded | Ascend |
| 5 | DESCEND | (ignored) | Descend |

### JOIN (type = 0x01) — 5 bytes total

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | type = 0x01 |
| 1 | uint16 LE | size = 5 |
| 3 | uint8 | EntityType (0=PLAYER, 1=GHOST_PLAYER) |

Must be the first message after WebSocket connection.

## Game EntityType values (for spawn/join)

| Value | Name | Physics |
|-------|------|---------|
| 0 | PLAYER | Full: gravity + swept AABB collision |
| 1 | GHOST_PLAYER | Ghost: velocity-only, no gravity, no collision |
| 2 | SHEEP | Passive mob: wanders randomly |
