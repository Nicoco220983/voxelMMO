#include <catch2/catch_test_macros.hpp>
#include "common/NetworkProtocol.hpp"
#include "common/MessageTypes.hpp"
#include "common/EntityType.hpp"

using namespace voxelmmo;

// ── parseInput Tests ─────────────────────────────────────────────────────────

TEST_CASE("parseInput extracts buttons, yaw, pitch correctly", "[network]") {
    uint8_t data[13] = {
        static_cast<uint8_t>(ClientMessageType::INPUT),  // type
        13, 0,                                           // size = 13
        0x0F,                                            // buttons (forward+back+left+right)
        0x00, 0x00, 0x80, 0x3F,                         // yaw = 1.0f (little-endian)
        0x00, 0x00, 0x00, 0x40                          // pitch = 2.0f (little-endian)
    };
    
    auto msg = NetworkProtocol::parseInput(data, sizeof(data));
    
    REQUIRE(msg.has_value());
    CHECK(msg->buttons == 0x0F);
    CHECK(msg->yaw == 1.0f);
    CHECK(msg->pitch == 2.0f);
}

TEST_CASE("parseInput returns nullopt for short buffer", "[network]") {
    uint8_t data[5] = {0, 5, 0, 0, 0};  // Too short
    auto msg = NetworkProtocol::parseInput(data, sizeof(data));
    REQUIRE_FALSE(msg.has_value());
}

TEST_CASE("parseInput returns nullopt for empty buffer", "[network]") {
    auto msg = NetworkProtocol::parseInput(nullptr, 0);
    REQUIRE_FALSE(msg.has_value());
}

TEST_CASE("parseInput handles zeroed input", "[network]") {
    uint8_t data[13] = {0, 13, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto msg = NetworkProtocol::parseInput(data, sizeof(data));
    
    REQUIRE(msg.has_value());
    CHECK(msg->buttons == 0);
    CHECK(msg->yaw == 0.0f);
    CHECK(msg->pitch == 0.0f);
}

// ── parseJoin Tests ──────────────────────────────────────────────────────────

TEST_CASE("parseJoin extracts GHOST_PLAYER entity type", "[network]") {
    uint8_t data[5] = {
        static_cast<uint8_t>(ClientMessageType::JOIN),
        5, 0,
        static_cast<uint8_t>(EntityType::GHOST_PLAYER)
    };
    auto msg = NetworkProtocol::parseJoin(data, sizeof(data));
    
    REQUIRE(msg.has_value());
    CHECK(msg->entityType == EntityType::GHOST_PLAYER);
}

TEST_CASE("parseJoin extracts PLAYER entity type", "[network]") {
    uint8_t data[5] = {
        static_cast<uint8_t>(ClientMessageType::JOIN),
        5, 0,
        static_cast<uint8_t>(EntityType::PLAYER)
    };
    auto msg = NetworkProtocol::parseJoin(data, sizeof(data));
    
    REQUIRE(msg.has_value());
    CHECK(msg->entityType == EntityType::PLAYER);
}

TEST_CASE("parseJoin returns nullopt for short buffer", "[network]") {
    uint8_t data[3] = {1, 3, 0};
    auto msg = NetworkProtocol::parseJoin(data, sizeof(data));
    REQUIRE_FALSE(msg.has_value());
}

// ── buildSelfEntityMessage Tests ─────────────────────────────────────────────

TEST_CASE("buildSelfEntityMessage has correct format", "[network]") {
    auto msg = NetworkProtocol::buildSelfEntityMessage(42, 100);
    
    REQUIRE(msg.size() == 13);
    CHECK(msg[0] == static_cast<uint8_t>(ServerMessageType::SELF_ENTITY));
    CHECK(msg[1] == 13);  // size low byte
    CHECK(msg[2] == 0);   // size high byte
    
    // Entity ID at offset 3 (uint32 LE)
    uint32_t entityId;
    std::memcpy(&entityId, &msg[3], sizeof(uint32_t));
    CHECK(entityId == 42);
    
    // Tick at offset 7 (uint32 LE)
    uint32_t tick;
    std::memcpy(&tick, &msg[7], sizeof(uint32_t));
    CHECK(tick == 100);
}

TEST_CASE("buildSelfEntityMessage handles max values", "[network]") {
    auto msg = NetworkProtocol::buildSelfEntityMessage(
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max()
    );
    
    uint32_t entityId, tick;
    std::memcpy(&entityId, &msg[3], sizeof(uint32_t));
    std::memcpy(&tick, &msg[7], sizeof(uint32_t));
    
    CHECK(entityId == std::numeric_limits<uint32_t>::max());
    CHECK(tick == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("buildSelfEntityMessage handles zero values", "[network]") {
    auto msg = NetworkProtocol::buildSelfEntityMessage(0, 0);
    
    uint32_t entityId, tick;
    std::memcpy(&entityId, &msg[3], sizeof(uint32_t));
    std::memcpy(&tick, &msg[7], sizeof(uint32_t));
    
    CHECK(entityId == 0);
    CHECK(tick == 0);
}

// ── appendToBatch Tests ──────────────────────────────────────────────────────

TEST_CASE("appendToBatch adds message to buffer", "[network]") {
    std::vector<uint8_t> batch;
    uint8_t msg[] = {1, 2, 3, 4, 5};
    
    NetworkProtocol::appendToBatch(batch, msg, sizeof(msg));
    
    REQUIRE(batch.size() == 5);
    CHECK(batch[0] == 1);
    CHECK(batch[4] == 5);
}

TEST_CASE("appendToBatch handles empty message", "[network]") {
    std::vector<uint8_t> batch = {1, 2, 3};
    NetworkProtocol::appendToBatch(batch, nullptr, 0);
    
    REQUIRE(batch.size() == 3);  // Unchanged
}

TEST_CASE("appendToBatch concatenates multiple messages", "[network]") {
    std::vector<uint8_t> batch;
    uint8_t msg1[] = {1, 2};
    uint8_t msg2[] = {3, 4, 5};
    
    NetworkProtocol::appendToBatch(batch, msg1, sizeof(msg1));
    NetworkProtocol::appendToBatch(batch, msg2, sizeof(msg2));
    
    REQUIRE(batch.size() == 5);
    CHECK(batch[0] == 1);
    CHECK(batch[2] == 3);
    CHECK(batch[4] == 5);
}

TEST_CASE("appendToBatch vector overload works", "[network]") {
    std::vector<uint8_t> batch;
    std::vector<uint8_t> msg = {1, 2, 3};
    
    NetworkProtocol::appendToBatch(batch, msg);
    
    REQUIRE(batch.size() == 3);
    CHECK(batch == msg);
}
