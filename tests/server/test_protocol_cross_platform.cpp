#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "common/NetworkProtocol.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/EntityType.hpp"
#include "TestUtils.hpp"

using namespace voxelmmo;
using Catch::Approx;

/**
 * @file test_protocol_cross_platform.cpp
 * @brief Cross-platform protocol validation tests.
 * 
 * These tests validate that:
 * 1. Server can parse all client-to-server fixtures (produced by client tests)
 * 2. Server produces messages matching server-to-client fixtures (consumed by client tests)
 * 
 * This ensures wire-format compatibility between C++ server and JS client.
 */

// ── Client-to-Server Fixture Parsing ─────────────────────────────────────────

TEST_CASE("Server parses all INPUT fixtures correctly", "[protocol][cross-platform]") {
    SECTION("input_zero.hex") {
        auto data = loadHexFixture("client_to_server/input/input_zero.hex");
        auto msg = NetworkProtocol::parseInput(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->inputType == InputType::MOVE);
        CHECK(msg->buttons == 0);
        CHECK(msg->yaw == 0.0f);
        CHECK(msg->pitch == 0.0f);
    }
    
    SECTION("input_forward.hex") {
        auto data = loadHexFixture("client_to_server/input/input_forward.hex");
        auto msg = NetworkProtocol::parseInput(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->inputType == InputType::MOVE);
        CHECK(msg->buttons == static_cast<uint8_t>(InputButton::FORWARD));
        CHECK(msg->yaw == 0.0f);
        CHECK(msg->pitch == 0.0f);
    }
    
    SECTION("input_jump.hex") {
        auto data = loadHexFixture("client_to_server/input/input_jump.hex");
        auto msg = NetworkProtocol::parseInput(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->inputType == InputType::MOVE);
        CHECK(msg->buttons == static_cast<uint8_t>(InputButton::JUMP));
    }
    
    SECTION("input_all_buttons.hex") {
        auto data = loadHexFixture("client_to_server/input/input_all_buttons.hex");
        auto msg = NetworkProtocol::parseInput(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->inputType == InputType::MOVE);
        CHECK(msg->buttons == 0x3F);  // All 6 button bits set
    }
    
    SECTION("input_yaw_pitch.hex") {
        auto data = loadHexFixture("client_to_server/input/input_yaw_pitch.hex");
        auto msg = NetworkProtocol::parseInput(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->inputType == InputType::MOVE);
        CHECK(msg->yaw == 1.0f);
        CHECK(msg->pitch == 2.0f);
    }
    
    SECTION("input_complex.hex") {
        auto data = loadHexFixture("client_to_server/input/input_complex.hex");
        auto msg = NetworkProtocol::parseInput(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->inputType == InputType::MOVE);
        CHECK(msg->buttons == (static_cast<uint8_t>(InputButton::FORWARD) | 
                               static_cast<uint8_t>(InputButton::LEFT)));
        CHECK(msg->yaw == Approx(3.14159f).margin(0.00001f));
        CHECK(msg->pitch == -0.5f);
    }
}

TEST_CASE("Server parses all JOIN fixtures correctly", "[protocol][cross-platform]") {
    SECTION("join_player.hex") {
        auto data = loadHexFixture("client_to_server/join/join_player.hex");
        auto msg = NetworkProtocol::parseJoin(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->entityType == EntityType::PLAYER);
    }
    
    SECTION("join_ghost.hex") {
        auto data = loadHexFixture("client_to_server/join/join_ghost.hex");
        auto msg = NetworkProtocol::parseJoin(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->entityType == EntityType::GHOST_PLAYER);
    }
    
    SECTION("join_sheep.hex") {
        auto data = loadHexFixture("client_to_server/join/join_sheep.hex");
        auto msg = NetworkProtocol::parseJoin(data.data(), data.size());
        
        REQUIRE(msg.has_value());
        CHECK(msg->entityType == EntityType::SHEEP);
    }
}

// ── Server-to-Client Fixture Generation ─────────────────────────────────────

TEST_CASE("Server SELF_ENTITY generation matches fixtures", "[protocol][cross-platform]") {
    SECTION("self_entity_zero.hex") {
        auto expected = loadHexFixture("server_to_client/self_entity/self_entity_zero.hex");
        auto actual = NetworkProtocol::buildSelfEntityMessage(0, 0);
        
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            CHECK(actual[i] == expected[i]);
        }
    }
    
    SECTION("self_entity_sample.hex") {
        auto expected = loadHexFixture("server_to_client/self_entity/self_entity_sample.hex");
        auto actual = NetworkProtocol::buildSelfEntityMessage(42, 100);
        
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            CHECK(actual[i] == expected[i]);
        }
    }
    
    SECTION("self_entity_max.hex") {
        auto expected = loadHexFixture("server_to_client/self_entity/self_entity_max.hex");
        auto actual = NetworkProtocol::buildSelfEntityMessage(
            std::numeric_limits<uint32_t>::max(),
            std::numeric_limits<uint32_t>::max()
        );
        
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            CHECK(actual[i] == expected[i]);
        }
    }
}

// ── Message Header Validation ────────────────────────────────────────────────

TEST_CASE("All fixtures have correct message headers", "[protocol][cross-platform]") {
    SECTION("INPUT fixtures") {
        auto fixtures = {
            "client_to_server/input/input_zero.hex",
            "client_to_server/input/input_forward.hex",
            "client_to_server/input/input_jump.hex",
            "client_to_server/input/input_all_buttons.hex",
            "client_to_server/input/input_yaw_pitch.hex",
            "client_to_server/input/input_complex.hex",
        };
        
        for (const auto& fixture : fixtures) {
            auto data = loadHexFixture(fixture);
            
            INFO("Checking header for: " << fixture);
            REQUIRE(data.size() >= 3);
            CHECK(data[0] == static_cast<uint8_t>(ClientMessageType::INPUT));
            CHECK(data[1] == 14);  // size low byte
            CHECK(data[2] == 0);   // size high byte
        }
    }
    
    SECTION("JOIN fixtures") {
        auto fixtures = {
            "client_to_server/join/join_player.hex",
            "client_to_server/join/join_ghost.hex",
            "client_to_server/join/join_sheep.hex",
        };
        
        for (const auto& fixture : fixtures) {
            auto data = loadHexFixture(fixture);
            
            INFO("Checking header for: " << fixture);
            REQUIRE(data.size() >= 3);
            CHECK(data[0] == static_cast<uint8_t>(ClientMessageType::JOIN));
            CHECK(data[1] == 5);   // size low byte
            CHECK(data[2] == 0);   // size high byte
        }
    }
    
    SECTION("SELF_ENTITY fixtures") {
        auto fixtures = {
            "server_to_client/self_entity/self_entity_zero.hex",
            "server_to_client/self_entity/self_entity_sample.hex",
            "server_to_client/self_entity/self_entity_max.hex",
        };
        
        for (const auto& fixture : fixtures) {
            auto data = loadHexFixture(fixture);
            
            INFO("Checking header for: " << fixture);
            REQUIRE(data.size() >= 3);
            CHECK(data[0] == static_cast<uint8_t>(ServerMessageType::SELF_ENTITY));
            CHECK(data[1] == 13);  // size low byte
            CHECK(data[2] == 0);   // size high byte
        }
    }
}

// ── Endianness Validation ────────────────────────────────────────────────────

TEST_CASE("Fixtures use correct little-endian encoding", "[protocol][cross-platform]") {
    SECTION("INPUT yaw/pitch are little-endian float32") {
        auto data = loadHexFixture("client_to_server/input/input_yaw_pitch.hex");
        
        // inputType at offset 3: MOVE = 0
        CHECK(data[3] == 0x00);
        
        // buttons at offset 4: 0
        CHECK(data[4] == 0x00);
        
        // yaw = 1.0f in little-endian: 0x00 0x00 0x80 0x3f
        CHECK(data[5] == 0x00);
        CHECK(data[6] == 0x00);
        CHECK(data[7] == 0x80);
        CHECK(data[8] == 0x3f);
        
        // pitch = 2.0f in little-endian: 0x00 0x00 0x00 0x40
        CHECK(data[9] == 0x00);
        CHECK(data[10] == 0x00);
        CHECK(data[11] == 0x00);
        CHECK(data[12] == 0x40);
    }
    
    SECTION("SELF_ENTITY entityId/tick are little-endian uint32") {
        auto data = loadHexFixture("server_to_client/self_entity/self_entity_sample.hex");
        
        // entityId = 42 in little-endian: 0x2a 0x00 0x00 0x00
        CHECK(data[3] == 0x2a);
        CHECK(data[4] == 0x00);
        CHECK(data[5] == 0x00);
        CHECK(data[6] == 0x00);
        
        // tick = 100 in little-endian: 0x64 0x00 0x00 0x00
        CHECK(data[7] == 0x64);
        CHECK(data[8] == 0x00);
        CHECK(data[9] == 0x00);
        CHECK(data[10] == 0x00);
    }
}

// ── Round-trip Validation ────────────────────────────────────────────────────

TEST_CASE("Server round-trip: parse then regenerate matches original", "[protocol][cross-platform]") {
    SECTION("SELF_ENTITY round-trip") {
        // Start with fixture bytes
        auto original = loadHexFixture("server_to_client/self_entity/self_entity_sample.hex");
        
        // Parse the fixture
        auto view = std::make_unique<uint8_t[]>(original.size());
        std::memcpy(view.get(), original.data(), original.size());
        
        // Extract values from the message
        uint32_t entityId, tick;
        std::memcpy(&entityId, &view[3], sizeof(uint32_t));
        std::memcpy(&tick, &view[7], sizeof(uint32_t));
        
        // Regenerate using server code
        auto regenerated = NetworkProtocol::buildSelfEntityMessage(entityId, tick);
        
        // Should match original
        REQUIRE(regenerated.size() == original.size());
        for (size_t i = 0; i < regenerated.size(); ++i) {
            CHECK(regenerated[i] == original[i]);
        }
    }
}
