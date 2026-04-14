#pragma once
#include "common/Types.hpp"
#include "common/EntityType.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace voxelmmo {

// ── Enums (merged from MessageTypes.hpp) ────────────────────────────────────

/**
 * @brief Server message type - first byte of every server → client network message.
 *
 * All server messages start with [type][size] header (3 bytes total):
 *   - type: ServerMessageType (uint8)
 *   - size: uint16 LE (total message size including header)
 *
 * Chunk state messages (types 0-5) are followed by [chunk_id(8)][tick(4)] (12 bytes).
 *
 * Odd values are the LZ4-compressed counterpart of the preceding even value.
 * The header (type + size + ChunkId + tick) is always left uncompressed
 * so the gateway can route messages without decompressing them.
 */
enum class ServerMessageType : uint8_t {
    CHUNK_SNAPSHOT                  = 0,  ///< Full chunk state (voxels + entities)
    CHUNK_SNAPSHOT_COMPRESSED       = 1,  ///< LZ4-compressed snapshot
    CHUNK_SNAPSHOT_DELTA            = 2,  ///< Delta since last snapshot
    CHUNK_SNAPSHOT_DELTA_COMPRESSED = 3,  ///< LZ4-compressed snapshot delta
    CHUNK_TICK_DELTA                = 4,  ///< Per-tick delta
    CHUNK_TICK_DELTA_COMPRESSED     = 5,  ///< LZ4-compressed tick delta
    SELF_ENTITY                     = 6,  ///< Self-identification with GlobalEntityId
};

/** @brief Entity delta sub-type, single value per entity record in a delta. */
enum class DeltaType : uint8_t {
    CREATE_ENTITY       = 0,  ///< Entity appears in this chunk (newly spawned or moved from elsewhere).
    UPDATE_ENTITY       = 1,  ///< Entity already known in this chunk; only dirty components are present.
    DELETE_ENTITY       = 2,  ///< Entity removed from this chunk (despawned or moved elsewhere).
    CHUNK_CHANGE_ENTITY = 3,  ///< Entity moved to different chunk; old chunk sends this with new ChunkId.
};

/** @brief First byte of every client → server binary WebSocket frame. */
enum class ClientMessageType : uint8_t {
    INPUT = 0,  ///< inputType uint8 + payload (variable size based on input type)
    JOIN  = 1,  ///< EntityType uint8 — 1 byte payload (total 5 with header)
};

/** @brief Bitmask flags for the INPUT message buttons field. */
enum class InputButton : uint8_t {
    FORWARD  = 1 << 0,
    BACKWARD = 1 << 1,
    LEFT     = 1 << 2,
    RIGHT    = 1 << 3,
    JUMP     = 1 << 4,
    DESCEND  = 1 << 5,
};

/** @brief Input type - determines how the server interprets the input. */
enum class InputType : uint8_t {
    MOVE = 0,               ///< Movement input: buttons(1) + yaw(4) + pitch(4) = 10 bytes payload
    VOXEL_DESTROY = 1,      ///< Voxel destroy: vx(4) + vy(4) + vz(4) = 12 bytes payload
    VOXEL_CREATE = 2,       ///< Voxel create: vx(4) + vy(4) + vz(4) + voxelType(1) = 13 bytes payload
    BULK_VOXEL_DESTROY = 3, ///< Bulk voxel destroy: startX(4)+Y(4)+Z(4) + endX(4)+Y(4)+Z(4) = 24 bytes payload
    BULK_VOXEL_CREATE = 4,  ///< Bulk voxel create: startX(4)+Y(4)+Z(4) + endX(4)+Y(4)+Z(4) + voxelType(1) = 25 bytes payload
    TOOL_USE = 5,           ///< Tool use: toolId(1) + yaw(4) + pitch(4) = 10 bytes payload
    TOOL_SELECT = 6,        ///< Tool selection: toolId(1) = 1 byte payload
};

/**
 * @brief Serialization and deserialization helpers for the client↔server wire protocol.
 *
 * All methods are stateless and side-effect-free — they only read/write bytes.
 * Game logic (dispatching, registry updates) remains in GameEngine.
 */
namespace NetworkProtocol {

// ── Message header constants ──────────────────────────────────────────────

/** @brief Size of the universal message header: [type(1)][size(2)] = 3 bytes. */
inline constexpr size_t MESSAGE_HEADER_SIZE = 3;

/** @brief Size of chunk message additional header: [chunk_id(8)][tick(4)] = 12 bytes. */
inline constexpr size_t CHUNK_HEADER_SIZE = 12;

/** @brief Total chunk message header size: 3 + 12 = 15 bytes. */
inline constexpr size_t CHUNK_MESSAGE_HEADER_SIZE = MESSAGE_HEADER_SIZE + CHUNK_HEADER_SIZE;

// ── Parsed message structs ────────────────────────────────────────────────

/** Parsed payload of a ClientMessageType::INPUT frame. */
struct InputMessage {
    InputType inputType;  ///< Type of input (determines which fields are valid)
    // MOVE type fields:
    uint8_t   buttons;  ///< InputButton bitmask (valid if inputType == MOVE)
    float     yaw;      ///< radians (valid if inputType == MOVE or TOOL_USE)
    float     pitch;    ///< radians (valid if inputType == MOVE or TOOL_USE)
    // VOXEL_DESTROY/VOXEL_CREATE type fields:
    int32_t   vx;       ///< World voxel X (valid if inputType == VOXEL_DESTROY or VOXEL_CREATE)
    int32_t   vy;       ///< World voxel Y (valid if inputType == VOXEL_DESTROY or VOXEL_CREATE)
    int32_t   vz;       ///< World voxel Z (valid if inputType == VOXEL_DESTROY or VOXEL_CREATE)
    // VOXEL_CREATE/BULK_VOXEL_CREATE type fields:
    VoxelType voxelType; ///< Voxel type to create (valid if inputType == VOXEL_CREATE or BULK_VOXEL_CREATE)
    // BULK_VOXEL_DESTROY/BULK_VOXEL_CREATE type fields:
    int32_t   startX;   ///< Start voxel X (valid for bulk types)
    int32_t   startY;   ///< Start voxel Y (valid for bulk types)
    int32_t   startZ;   ///< Start voxel Z (valid for bulk types)
    int32_t   endX;     ///< End voxel X (valid for bulk types)
    int32_t   endY;     ///< End voxel Y (valid for bulk types)
    int32_t   endZ;     ///< End voxel Z (valid for bulk types)
    // TOOL_USE/TOOL_SELECT type fields:
    uint8_t   toolId;   ///< Tool type ID (valid if inputType == TOOL_USE or TOOL_SELECT)
};

/** Parsed payload of a ClientMessageType::JOIN frame. */
struct JoinMessage {
    EntityType entityType;
    /** Session token (16-byte UUID) for entity recovery across reconnects. Zeroed = no previous session. */
    std::array<uint8_t, 16> sessionToken;
};

/**
 * @brief Derive PlayerId deterministically from session token.
 * 
 * Uses the first 8 bytes of the 16-byte session token as the PlayerId.
 * This allows stateless player identification - the same session token
 * always maps to the same PlayerId, enabling automatic reconnection
 * without server-side session-to-player mapping.
 * 
 * @param token 16-byte session token (UUID).
 * @return PlayerId derived from first 8 bytes (little-endian).
 */
inline PlayerId playerIdFromSessionToken(const std::array<uint8_t, 16>& token) {
    PlayerId pid;
    std::memcpy(&pid, token.data(), sizeof(PlayerId));
    return pid;
}

// ── Client → Server (deserialization) ────────────────────────────────────

/**
 * @brief Parse an INPUT frame.
 * Wire format varies by inputType:
 *   MOVE:               type(1) + size(2) + inputType(1) + buttons(1) + yaw float32LE(4) + pitch float32LE(4) = 14 bytes
 *   VOXEL_DESTROY:      type(1) + size(2) + inputType(1) + vx int32LE(4) + vy int32LE(4) + vz int32LE(4) = 16 bytes
 *   VOXEL_CREATE:       type(1) + size(2) + inputType(1) + vx int32LE(4) + vy int32LE(4) + vz int32LE(4) + voxelType(1) = 17 bytes
 *   BULK_VOXEL_DESTROY: type(1) + size(2) + inputType(1) + startX(4)+Y(4)+Z(4) + endX(4)+Y(4)+Z(4) = 28 bytes
 *   BULK_VOXEL_CREATE:  type(1) + size(2) + inputType(1) + startX(4)+Y(4)+Z(4) + endX(4)+Y(4)+Z(4) + voxelType(1) = 29 bytes
 * @return Parsed struct, or std::nullopt if size is insufficient for the inputType.
 */
inline std::optional<InputMessage> parseInput(const uint8_t* data, size_t size) {
    if (size < 4) return std::nullopt;  // Need at least header + inputType
    
    InputMessage m;
    m.inputType = static_cast<InputType>(data[3]);
    
    switch (m.inputType) {
        case InputType::MOVE:
            if (size < 14) return std::nullopt;
            m.buttons = data[4];
            std::memcpy(&m.yaw,   data + 5, sizeof(float));
            std::memcpy(&m.pitch, data + 9, sizeof(float));
            break;
            
        case InputType::VOXEL_DESTROY:
            if (size < 16) return std::nullopt;
            std::memcpy(&m.vx, data + 4, sizeof(int32_t));
            std::memcpy(&m.vy, data + 8, sizeof(int32_t));
            std::memcpy(&m.vz, data + 12, sizeof(int32_t));
            break;
            
        case InputType::VOXEL_CREATE:
            if (size < 17) return std::nullopt;
            std::memcpy(&m.vx, data + 4, sizeof(int32_t));
            std::memcpy(&m.vy, data + 8, sizeof(int32_t));
            std::memcpy(&m.vz, data + 12, sizeof(int32_t));
            m.voxelType = static_cast<VoxelType>(data[16]);
            break;
            
        case InputType::BULK_VOXEL_DESTROY:
            if (size < 28) return std::nullopt;
            std::memcpy(&m.startX, data + 4, sizeof(int32_t));
            std::memcpy(&m.startY, data + 8, sizeof(int32_t));
            std::memcpy(&m.startZ, data + 12, sizeof(int32_t));
            std::memcpy(&m.endX, data + 16, sizeof(int32_t));
            std::memcpy(&m.endY, data + 20, sizeof(int32_t));
            std::memcpy(&m.endZ, data + 24, sizeof(int32_t));
            break;
            
        case InputType::BULK_VOXEL_CREATE:
            if (size < 29) return std::nullopt;
            std::memcpy(&m.startX, data + 4, sizeof(int32_t));
            std::memcpy(&m.startY, data + 8, sizeof(int32_t));
            std::memcpy(&m.startZ, data + 12, sizeof(int32_t));
            std::memcpy(&m.endX, data + 16, sizeof(int32_t));
            std::memcpy(&m.endY, data + 20, sizeof(int32_t));
            std::memcpy(&m.endZ, data + 24, sizeof(int32_t));
            m.voxelType = static_cast<VoxelType>(data[28]);
            break;

        case InputType::TOOL_USE:
            if (size < 14) return std::nullopt;
            m.toolId = data[4];
            std::memcpy(&m.yaw,   data + 5, sizeof(float));
            std::memcpy(&m.pitch, data + 9, sizeof(float));
            break;

        case InputType::TOOL_SELECT:
            if (size < 5) return std::nullopt;
            m.toolId = data[4];
            break;
            
        default:
            return std::nullopt;  // Unknown input type
    }
    
    return m;
}

/**
 * @brief Parse a JOIN frame.
 * Wire: type(1) + size(2) + EntityType uint8(1) + sessionToken(16) = 21 bytes.
 * @return Parsed struct, or std::nullopt if @p size < 21.
 */
inline std::optional<JoinMessage> parseJoin(const uint8_t* data, size_t size) {
    if (size < 21) return std::nullopt;
    JoinMessage msg;
    msg.entityType = static_cast<EntityType>(data[3]);
    std::memcpy(msg.sessionToken.data(), data + 4, 16);
    return msg;
}

// ── Server → Client (serialization) ──────────────────────────────────────

/**
 * @brief Build a SELF_ENTITY message (13 bytes).
 * Wire: type(1) + size(2) + GlobalEntityId uint32LE(4) + tick uint32LE(4) + reserved uint32LE(4).
 * The reserved field is for future use (e.g., player flags); clients must ignore it.
 */
inline std::array<uint8_t, 13> buildSelfEntityMessage(
    GlobalEntityId entityId, uint32_t tick)
{
    std::array<uint8_t, 13> msg;
    msg[0] = static_cast<uint8_t>(ServerMessageType::SELF_ENTITY);
    msg[1] = 13;  // size low byte
    msg[2] = 0;   // size high byte
    std::memcpy(msg.data() + 3, &entityId, sizeof(uint32_t));
    std::memcpy(msg.data() + 7, &tick, sizeof(uint32_t));
    // bytes 11-14: reserved for future use (flags, etc.)
    msg[11] = 0;
    msg[12] = 0;
    return msg;
}

// ── Batch helpers ─────────────────────────────────────────────────────────

/**
 * @brief Append a message to a batch buffer by direct concatenation.
 * Messages are concatenated as-is; each message already has a [type][size] header.
 * No-op if @p size is 0.
 */
inline void appendToBatch(std::vector<uint8_t>& buf, const uint8_t* data, size_t size) {
    if (size == 0) return;
    buf.insert(buf.end(), data, data + size);
}

/** @brief Convenience overload for a full vector. */
inline void appendToBatch(std::vector<uint8_t>& buf, const std::vector<uint8_t>& msg) {
    appendToBatch(buf, msg.data(), msg.size());
}

} // namespace NetworkProtocol
} // namespace voxelmmo
