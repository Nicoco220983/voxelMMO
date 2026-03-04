# voxelmmo — Wire format reference

> Keep this file up to date with every protocol change.
> It is read by AI agents: keep it concise and accurate.

## Chunk state message header (13 bytes, always uncompressed)

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | ChunkMessageType |
| 1 | int64 LE | ChunkId packed |
| 9 | uint32 LE | server tick when built |

## ChunkMessageType values

| Value | Name |
|-------|------|
| 0 | SNAPSHOT |
| 1 | SNAPSHOT_COMPRESSED |
| 2 | SNAPSHOT_DELTA |
| 3 | SNAPSHOT_DELTA_COMPRESSED |
| 4 | TICK_DELTA |
| 5 | TICK_DELTA_COMPRESSED |

Odd = LZ4-compressed counterpart of the preceding even value.

## Snapshot (type = SNAPSHOT_COMPRESSED)

```
[13-byte header]
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

### Entity record (snapshot)

```
GlobalEntityId uint32    (stable across chunk moves and server lifetime)
EntityType     uint8
ComponentFlags uint8    (bitmask of present components)
  if POSITION_BIT:  DynamicPositionComponent fields (see below)
```

## Delta (SNAPSHOT_DELTA / TICK_DELTA, optionally _COMPRESSED)

```
[13-byte header]
  if _COMPRESSED:
    int32   uncompressed_payload_size
    bytes   LZ4(payload)
  else payload:
    int32   voxel_count
    repeat: VoxelIndex uint16 (uint5 y | uint5 x | uint5 z) · VoxelType uint8
    int32   entity_count
    repeat: entity delta record
```

### Entity delta record

```
DeltaType      uint8   (0=CREATE, 1=UPDATE, 2=DELETE, 3=CHUNK_CHANGE)
GlobalEntityId uint32  (stable across chunk moves and server lifetime)
  if DeltaType == CREATE or UPDATE:
    EntityType     uint8
    ComponentFlags uint8   (bitmask of dirty components, bits 0-5)
      if POSITION_BIT:       DynamicPositionComponent fields (see below)
      if SHEEP_BEHAVIOR_BIT: SheepBehaviorComponent fields (see below)
  if DeltaType == CHUNK_CHANGE:
    NewChunkId     int64 LE (packed) - the chunk the entity moved to
  if DeltaType == DELETE:
    (no additional fields)
```

**DeltaType semantics:**
- **CREATE_ENTITY (0)**: Entity appears in this chunk for the first time (newly spawned or arrived from another chunk). Full entity data follows.
- **UPDATE_ENTITY (1)**: Entity already known in this chunk; only dirty components are present.
- **DELETE_ENTITY (2)**: Entity removed from this chunk (despawned or moved elsewhere). No payload.
- **CHUNK_CHANGE_ENTITY (3)**: Entity moved to a different chunk. The *old* chunk sends this with the new ChunkId. Clients should update entity.chunkId but keep the entity object.

## DynamicPositionComponent fields (when POSITION_BIT set)

```
int32 x, y, z     sub-voxel position (server keeps always current)
int32 vx, vy, vz  sub-voxels per tick
uint8 grounded    0 = airborne (gravity applied), 1 = on ground
```

Total: 25 bytes. Tick is NOT here — read from the message header.

## GlobalEntityId semantics

`GlobalEntityId` (uint32) is assigned once at entity spawn and is stable for
the entire lifetime of the entity across all chunk moves. The client can track
entities reliably using this ID; no DELETE+NEW cycle occurs on chunk crossing.

IDs are assigned monotonically starting from 1 (0 reserved) and persist across
save/resume cycles by persisting the counter with the save file.

## Client → Server (binary WebSocket frames)

All frames start with a `ClientMessageType` byte.

### INPUT (type = 0x00) — 10 bytes total

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | type = 0x00 |
| 1 | uint8 | InputButton bitmask |
| 2 | float32 LE | yaw (radians, camera horizontal) |
| 6 | float32 LE | pitch (radians, camera vertical) |

The server's `InputSystem::apply()` translates buttons + yaw/pitch → velocity each tick before physics.

#### InputButton bitmask

| Bit | Name | PLAYER effect | GHOST_PLAYER effect |
|-----|------|---------------|---------------------|
| 0 | FORWARD | Move forward (yaw, horizontal) | Move forward (yaw+pitch, 3D) |
| 1 | BACKWARD | Move backward | Move backward |
| 2 | LEFT | Strafe left | Strafe left |
| 3 | RIGHT | Strafe right | Strafe right |
| 4 | JUMP | Jump impulse when grounded | Ascend |
| 5 | DESCEND | (ignored) | Descend |

### JOIN (type = 0x01) — 2 bytes total

| Offset | Size | Field |
|--------|------|-------|
| 0 | uint8 | type = 0x01 |
| 1 | uint8 | EntityType (0 = PLAYER, 1 = GHOST_PLAYER) |

Must be the first message sent after the WebSocket connection is open.
The server spawns the entity and sends the initial snapshot in response.

URL param: `?mode=ghost` → GHOST_PLAYER; default (bare URL) → PLAYER.

#### EntityType values

| Value | Name | Physics |
|-------|------|---------|
| 0 | PLAYER | Full: gravity + swept AABB collision |
| 1 | GHOST_PLAYER | Ghost: velocity-only, no gravity, no collision |
