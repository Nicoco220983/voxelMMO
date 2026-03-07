#pragma once
#include "common/MessageTypes.hpp"
#include "common/Types.hpp"
#include "common/EntityType.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

namespace voxelmmo {

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
    uint8_t buttons;  ///< InputButton bitmask
    float   yaw;      ///< radians
    float   pitch;    ///< radians
};

/** Parsed payload of a ClientMessageType::JOIN frame. */
struct JoinMessage {
    EntityType entityType;
};

// ── Client → Server (deserialization) ────────────────────────────────────

/**
 * @brief Parse an INPUT frame.
 * Wire: type(1) + size(2) + buttons uint8(1) + yaw float32LE(4) + pitch float32LE(4) = 13 bytes.
 * @return Parsed struct, or std::nullopt if @p size < 13.
 */
inline std::optional<InputMessage> parseInput(const uint8_t* data, size_t size) {
    if (size < 13) return std::nullopt;
    InputMessage m;
    m.buttons = data[3];
    std::memcpy(&m.yaw,   data + 4, sizeof(float));
    std::memcpy(&m.pitch, data + 8, sizeof(float));
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
 * @brief Build a SELF_ENTITY message (21 bytes).
 * Wire: type(1) + size(2) + GlobalEntityId uint32LE(4) + ChunkId int64LE(8) + tick uint32LE(4).
 */
inline std::array<uint8_t, 21> buildSelfEntityMessage(
    GlobalEntityId entityId, const ChunkId& chunkId, uint32_t tick)
{
    std::array<uint8_t, 21> msg;
    msg[0] = static_cast<uint8_t>(ServerMessageType::SELF_ENTITY);
    msg[1] = 21;  // size low byte
    msg[2] = 0;   // size high byte
    std::memcpy(msg.data() + 3, &entityId, sizeof(uint32_t));
    std::memcpy(msg.data() + 7, &chunkId.packed, 8);
    std::memcpy(msg.data() + 15, &tick, sizeof(uint32_t));
    return msg;
}

// ── Batch framing ─────────────────────────────────────────────────────────

/**
 * @brief Append a length-prefixed message to a batch buffer.
 * Wire format: uint32 msgLen (LE) | msgLen bytes.
 * No-op if @p size is 0.
 */
inline void appendFramed(std::vector<uint8_t>& buf, const uint8_t* data, size_t size) {
    if (size == 0) return;
    const uint32_t len      = static_cast<uint32_t>(size);
    const auto*    lenBytes = reinterpret_cast<const uint8_t*>(&len);
    buf.insert(buf.end(), lenBytes,  lenBytes + 4);
    buf.insert(buf.end(), data,      data + size);
}

/** @brief Convenience overload for a full vector. */
inline void appendFramed(std::vector<uint8_t>& buf, const std::vector<uint8_t>& msg) {
    appendFramed(buf, msg.data(), msg.size());
}

} // namespace NetworkProtocol
} // namespace voxelmmo
