#pragma once
#include "common/MessageTypes.hpp"
#include "common/Types.hpp"
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
 * Wire: type(1) + buttons uint8(1) + yaw float32LE(4) + pitch float32LE(4) = 10 bytes.
 * @return Parsed struct, or std::nullopt if @p size < 10.
 */
inline std::optional<InputMessage> parseInput(const uint8_t* data, size_t size) {
    if (size < 10) return std::nullopt;
    InputMessage m;
    m.buttons = data[1];
    std::memcpy(&m.yaw,   data + 2, sizeof(float));
    std::memcpy(&m.pitch, data + 6, sizeof(float));
    return m;
}

/**
 * @brief Parse a JOIN frame.
 * Wire: type(1) + EntityType uint8(1) = 2 bytes.
 * @return Parsed struct, or std::nullopt if @p size < 2.
 */
inline std::optional<JoinMessage> parseJoin(const uint8_t* data, size_t size) {
    if (size < 2) return std::nullopt;
    return JoinMessage{static_cast<EntityType>(data[1])};
}

// ── Server → Client (serialization) ──────────────────────────────────────

/**
 * @brief Build a 17-byte SELF_ENTITY message.
 * Wire: type(1) + ChunkId int64LE(8) + tick uint32LE(4) + GlobalEntityId uint32LE(4).
 */
inline std::array<uint8_t, 17> buildSelfEntityMessage(
    const ChunkId& chunkId, uint32_t tick, GlobalEntityId entityId)
{
    std::array<uint8_t, 17> msg;
    msg[0] = static_cast<uint8_t>(ChunkMessageType::SELF_ENTITY);
    std::memcpy(msg.data() + 1,  &chunkId.packed, 8);
    std::memcpy(msg.data() + 9,  &tick,            sizeof(uint32_t));
    std::memcpy(msg.data() + 13, &entityId,         sizeof(uint32_t));
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
