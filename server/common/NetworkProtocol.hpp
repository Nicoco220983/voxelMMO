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
    INPUT = 0,  ///< inputType uint8 + buttons uint8 + yaw float32 + pitch float32 — 10 bytes payload (total 14 with header)
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
    MOVE = 0,  ///< Movement input (standard walking/flying controls)
    // Future types: INTERACT, BUILD, DESTROY, etc.
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
    InputType inputType;  ///< Type of input (determines interpretation)
    uint8_t   buttons;  ///< InputButton bitmask
    float     yaw;      ///< radians
    float     pitch;    ///< radians
};

/** Parsed payload of a ClientMessageType::JOIN frame. */
struct JoinMessage {
    EntityType entityType;
};

// ── Client → Server (deserialization) ────────────────────────────────────

/**
 * @brief Parse an INPUT frame.
 * Wire: type(1) + size(2) + tool uint8(1) + buttons uint8(1) + yaw float32LE(4) + pitch float32LE(4) = 14 bytes.
 * @return Parsed struct, or std::nullopt if @p size < 14.
 */
inline std::optional<InputMessage> parseInput(const uint8_t* data, size_t size) {
    if (size < 14) return std::nullopt;
    InputMessage m;
    m.inputType = static_cast<InputType>(data[3]);
    m.buttons = data[4];
    std::memcpy(&m.yaw,   data + 5, sizeof(float));
    std::memcpy(&m.pitch, data + 9, sizeof(float));
    return m;
}

/**
 * @brief Parse a JOIN frame.
 * Wire: type(1) + size(2) + EntityType uint8(1) = 5 bytes.
 * @return Parsed struct, or std::nullopt if @p size < 5.
 */
inline std::optional<JoinMessage> parseJoin(const uint8_t* data, size_t size) {
    if (size < 5) return std::nullopt;
    return JoinMessage{static_cast<EntityType>(data[3])};
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
