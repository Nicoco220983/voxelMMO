#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common/NetworkProtocol.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/EntityType.hpp"
#include "TestUtils.hpp"

using Catch::Approx;

using namespace voxelmmo;

// ── parseInput Tests ─────────────────────────────────────────────────────────

TEST_CASE("parseInput extracts inputType, buttons, yaw, pitch correctly", "[network]") {
    auto data = loadHexFixture("client_to_server/input/input_all_buttons.hex");
    
    auto msg = NetworkProtocol::parseInput(data.data(), data.size());
    
    REQUIRE(msg.has_value());
    CHECK(msg->inputType == InputType::MOVE);
    CHECK(msg->buttons == 0x3F);  // All buttons: 0x01|0x02|0x04|0x08|0x10|0x20
    CHECK(msg->yaw == 0.0f);
    CHECK(msg->pitch == 0.0f);
}

TEST_CASE("parseInput handles forward button from fixture", "[network]") {
    auto data = loadHexFixture("client_to_server/input/input_forward.hex");
    
    auto msg = NetworkProtocol::parseInput(data.data(), data.size());
    
    REQUIRE(msg.has_value());
    CHECK(msg->inputType == InputType::MOVE);
    CHECK(msg->buttons == static_cast<uint8_t>(InputButton::FORWARD));
    CHECK(msg->yaw == 0.0f);
    CHECK(msg->pitch == 0.0f);
}

TEST_CASE("parseInput handles jump button from fixture", "[network]") {
    auto data = loadHexFixture("client_to_server/input/input_jump.hex");
    
    auto msg = NetworkProtocol::parseInput(data.data(), data.size());
    
    REQUIRE(msg.has_value());
    CHECK(msg->inputType == InputType::MOVE);
    CHECK(msg->buttons == static_cast<uint8_t>(InputButton::JUMP));
}

TEST_CASE("parseInput handles complex yaw/pitch from fixture", "[network]") {
    auto data = loadHexFixture("client_to_server/input/input_complex.hex");
    
    auto msg = NetworkProtocol::parseInput(data.data(), data.size());
    
    REQUIRE(msg.has_value());
    CHECK(msg->inputType == InputType::MOVE);
    CHECK(msg->buttons == (static_cast<uint8_t>(InputButton::FORWARD) | 
                           static_cast<uint8_t>(InputButton::LEFT)));
    CHECK(msg->yaw == Approx(3.14159f).margin(0.00001f));
    CHECK(msg->pitch == -0.5f);
}

TEST_CASE("parseInput handles zeroed input from fixture", "[network]") {
    auto data = loadHexFixture("client_to_server/input/input_zero.hex");
    
    auto msg = NetworkProtocol::parseInput(data.data(), data.size());
    
    REQUIRE(msg.has_value());
    CHECK(msg->inputType == InputType::MOVE);
    CHECK(msg->buttons == 0);
    CHECK(msg->yaw == 0.0f);
    CHECK(msg->pitch == 0.0f);
}

TEST_CASE("parseInput returns nullopt for short buffer", "[network]") {
    uint8_t data[6] = {0, 6, 0, 0, 0, 0};  // Too short (need 14)
    auto msg = NetworkProtocol::parseInput(data, sizeof(data));
    REQUIRE_FALSE(msg.has_value());
}

TEST_CASE("parseInput returns nullopt for empty buffer", "[network]") {
    auto msg = NetworkProtocol::parseInput(nullptr, 0);
    REQUIRE_FALSE(msg.has_value());
}

// ── parseJoin Tests ──────────────────────────────────────────────────────────

TEST_CASE("parseJoin extracts GHOST_PLAYER entity type from fixture", "[network]") {
    auto data = loadHexFixture("client_to_server/join/join_ghost.hex");
    
    auto msg = NetworkProtocol::parseJoin(data.data(), data.size());
    
    REQUIRE(msg.has_value());
    CHECK(msg->entityType == EntityType::GHOST_PLAYER);
}

TEST_CASE("parseJoin extracts PLAYER entity type from fixture", "[network]") {
    auto data = loadHexFixture("client_to_server/join/join_player.hex");
    
    auto msg = NetworkProtocol::parseJoin(data.data(), data.size());
    
    REQUIRE(msg.has_value());
    CHECK(msg->entityType == EntityType::PLAYER);
}

TEST_CASE("parseJoin extracts SHEEP entity type from fixture", "[network]") {
    auto data = loadHexFixture("client_to_server/join/join_sheep.hex");
    
    auto msg = NetworkProtocol::parseJoin(data.data(), data.size());
    
    REQUIRE(msg.has_value());
    CHECK(msg->entityType == EntityType::SHEEP);
}

TEST_CASE("parseJoin returns nullopt for short buffer", "[network]") {
    uint8_t data[3] = {1, 3, 0};
    auto msg = NetworkProtocol::parseJoin(data, sizeof(data));
    REQUIRE_FALSE(msg.has_value());
}

// ── buildSelfEntityMessage Tests ─────────────────────────────────────────────

TEST_CASE("buildSelfEntityMessage matches zero fixture", "[network]") {
    auto expected = loadHexFixture("server_to_client/self_entity/self_entity_zero.hex");
    auto msg = NetworkProtocol::buildSelfEntityMessage(0, 0);
    
    REQUIRE(msg.size() == expected.size());
    for (size_t i = 0; i < msg.size(); ++i) {
        CHECK(msg[i] == expected[i]);
    }
}

TEST_CASE("buildSelfEntityMessage matches sample fixture", "[network]") {
    auto expected = loadHexFixture("server_to_client/self_entity/self_entity_sample.hex");
    auto msg = NetworkProtocol::buildSelfEntityMessage(42, 100);
    
    REQUIRE(msg.size() == expected.size());
    for (size_t i = 0; i < msg.size(); ++i) {
        CHECK(msg[i] == expected[i]);
    }
}

TEST_CASE("buildSelfEntityMessage handles max values matching fixture", "[network]") {
    auto expected = loadHexFixture("server_to_client/self_entity/self_entity_max.hex");
    auto msg = NetworkProtocol::buildSelfEntityMessage(
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max()
    );
    
    REQUIRE(msg.size() == expected.size());
    for (size_t i = 0; i < msg.size(); ++i) {
        CHECK(msg[i] == expected[i]);
    }
}

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

// ── Fixture Loading Tests ────────────────────────────────────────────────────

TEST_CASE("loadHexFixture loads input fixtures correctly", "[network][fixtures]") {
    auto data = loadHexFixture("client_to_server/input/input_forward.hex");
    
    REQUIRE(data.size() == 14);
    CHECK(data[0] == 0);   // type = INPUT
    CHECK(data[1] == 14);  // size low
    CHECK(data[2] == 0);   // size high
    CHECK(data[3] == 0);   // inputType = MOVE
    CHECK(data[4] == 1);   // buttons = FORWARD
}

TEST_CASE("loadHexFixture loads join fixtures correctly", "[network][fixtures]") {
    auto data = loadHexFixture("client_to_server/join/join_player.hex");
    
    REQUIRE(data.size() == 5);
    CHECK(data[0] == 1);   // type = JOIN
    CHECK(data[1] == 5);   // size low
    CHECK(data[3] == 0);   // entityType = PLAYER
}

TEST_CASE("loadHexFixture loads self_entity fixtures correctly", "[network][fixtures]") {
    auto data = loadHexFixture("server_to_client/self_entity/self_entity_sample.hex");
    
    REQUIRE(data.size() == 13);
    CHECK(data[0] == 6);   // type = SELF_ENTITY
    CHECK(data[1] == 13);  // size low
}

TEST_CASE("loadHexFixture throws on missing file", "[network][fixtures]") {
    CHECK_THROWS(loadHexFixture("nonexistent/nonexistent.hex"));
}
