#include "TestUtils.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/systems/InputSystem.hpp"
#include "game/systems/PhysicsSystem.hpp"
#include "game/systems/JumpSystem.hpp"
#include "game/systems/ChunkMembershipSystem.hpp"
#include "game/components/ChunkMembershipComponent.hpp"

#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <cctype>

namespace voxelmmo {

// ── TestEnv Implementation ──────────────────────────────────────────────────

TestEnv::TestEnv(uint32_t seed)
    : engine_(seed, GeneratorType::NORMAL, true)
{
    // Register gateway and capture outputs
    engine_.registerGateway(gatewayId_);
    
    engine_.setOutputCallback([this](GatewayId gw, const uint8_t* data, size_t size) {
        onGatewayOutput(gw, data, size);
    });
    
    engine_.setPlayerOutputCallback([this](PlayerId pid, const uint8_t* data, size_t size) {
        onPlayerOutput(pid, data, size);
    });
}

PlayerId TestEnv::addPlayer(EntityType type) {
    // Generate a unique session token for this player
    // First 8 bytes will become the PlayerId, last 8 bytes are padding
    std::array<uint8_t, 16> sessionToken{};
    // Write a unique identifier in the first 4 bytes
    sessionToken[0] = static_cast<uint8_t>(nextPlayerIndex_ & 0xFF);
    sessionToken[1] = static_cast<uint8_t>((nextPlayerIndex_ >> 8) & 0xFF);
    sessionToken[2] = static_cast<uint8_t>((nextPlayerIndex_ >> 16) & 0xFF);
    sessionToken[3] = static_cast<uint8_t>((nextPlayerIndex_ >> 24) & 0xFF);
    nextPlayerIndex_++;
    
    // Derive PlayerId from session token (first 8 bytes)
    PlayerId pid;
    std::memcpy(&pid, sessionToken.data(), sizeof(PlayerId));
    
    // Queue as pending player (spawn position is determined by WorldGenerator)
    engine_.registerPlayer(gatewayId_, pid);
    
    // Send JOIN message to spawn the entity (21 bytes: type + size + entityType + sessionToken)
    uint8_t joinMsg[21] = {
        static_cast<uint8_t>(ClientMessageType::JOIN),
        21, 0,  // size
        static_cast<uint8_t>(type)
    };
    std::memcpy(joinMsg + 4, sessionToken.data(), 16);
    engine_.handlePlayerInput(pid, joinMsg, sizeof(joinMsg));
    
    // Track the entity (will be created on next tick)
    return pid;
}

// void TestEnv::removePlayer(PlayerId pid) {
//     engine_.removePlayer(pid);
//     playerEntities_.erase(pid);
//     playerOutputs_.erase(pid);
// }

void TestEnv::teleport(PlayerId pid, int32_t x, int32_t y, int32_t z) {
    engine_.teleportPlayer(pid, x, y, z);
}

void TestEnv::setInput(PlayerId pid, uint8_t buttons, float yaw, float pitch) {
    // Build INPUT message (14 bytes)
    uint8_t inputMsg[14];
    inputMsg[0] = static_cast<uint8_t>(ClientMessageType::INPUT);
    inputMsg[1] = 14;  // size low
    inputMsg[2] = 0;   // size high
    inputMsg[3] = static_cast<uint8_t>(InputType::MOVE);  // default input type
    inputMsg[4] = buttons;
    std::memcpy(&inputMsg[5], &yaw, sizeof(float));
    std::memcpy(&inputMsg[9], &pitch, sizeof(float));
    
    engine_.handlePlayerInput(pid, inputMsg, sizeof(inputMsg));
}

void TestEnv::pressButton(PlayerId pid, InputButton btn) {
    auto* input = getInput(pid);
    if (!input) return;
    uint8_t newButtons = input->buttons | static_cast<uint8_t>(btn);
    setInput(pid, newButtons, input->yaw, input->pitch);
}

void TestEnv::releaseButton(PlayerId pid, InputButton btn) {
    auto* input = getInput(pid);
    if (!input) return;
    uint8_t newButtons = input->buttons & ~static_cast<uint8_t>(btn);
    setInput(pid, newButtons, input->yaw, input->pitch);
}

void TestEnv::setLook(PlayerId pid, float yaw, float pitch) {
    auto* input = getInput(pid);
    if (!input) return;
    setInput(pid, input->buttons, yaw, pitch);
}

entt::entity TestEnv::getEntity(PlayerId pid) {
    // First check our cache
    auto it = playerEntities_.find(pid);
    if (it != playerEntities_.end()) {
        if (registry().valid(it->second)) {
            return it->second;
        }
        // Entity was destroyed, remove from cache
        playerEntities_.erase(it);
    }
    
    // Search registry for player
    auto view = registry().view<PlayerComponent>();
    for (auto ent : view) {
        if (view.get<PlayerComponent>(ent).playerId == pid) {
            playerEntities_[pid] = ent;
            return ent;
        }
    }
    
    return entt::null;
}

DynamicPositionComponent* TestEnv::getPosition(PlayerId pid) {
    auto ent = getEntity(pid);
    if (ent == entt::null) return nullptr;
    return registry().try_get<DynamicPositionComponent>(ent);
}

InputComponent* TestEnv::getInput(PlayerId pid) {
    auto ent = getEntity(pid);
    if (ent == entt::null) return nullptr;
    return registry().try_get<InputComponent>(ent);
}

bool TestEnv::hasPlayer(PlayerId pid) {
    return getEntity(pid) != static_cast<entt::entity>(entt::null);
}

Chunk* TestEnv::getChunk(int32_t cx, int32_t cy, int32_t cz) {
    return chunks().getChunkMutable(ChunkId::make(cy, cx, cz));
}

bool TestEnv::hasChunk(int32_t cx, int32_t cy, int32_t cz) {
    return chunks().hasChunk(ChunkId::make(cy, cx, cz));
}

Chunk* TestEnv::getChunkAt(int32_t x, int32_t y, int32_t z) {
    return chunks().getChunkMutable(ChunkId::fromSubVoxelPos(x, y, z));
}

void TestEnv::tick(int n) {
    for (int i = 0; i < n; ++i) {
        engine_.tick();
        ++tickCount_;
    }
}

std::vector<uint8_t> TestEnv::getPlayerOutput(PlayerId pid) const {
    auto it = playerOutputs_.find(pid);
    if (it != playerOutputs_.end()) {
        return it->second;
    }
    return {};
}

void TestEnv::clearOutputs() {
    gatewayOutput_.clear();
    playerOutputs_.clear();
}

void TestEnv::assertPosition(PlayerId pid, int32_t x, int32_t y, int32_t z, int32_t tolerance) {
    auto* pos = getPosition(pid);
    REQUIRE(pos != nullptr);
    CHECK(std::abs(pos->x - x) <= tolerance);
    CHECK(std::abs(pos->y - y) <= tolerance);
    CHECK(std::abs(pos->z - z) <= tolerance);
}

void TestEnv::assertPositionNear(PlayerId pid, int32_t x, int32_t y, int32_t z, int32_t maxDistance) {
    auto* pos = getPosition(pid);
    REQUIRE(pos != nullptr);
    int64_t dx = static_cast<int64_t>(pos->x) - x;
    int64_t dy = static_cast<int64_t>(pos->y) - y;
    int64_t dz = static_cast<int64_t>(pos->z) - z;
    int64_t dist = static_cast<int64_t>(std::sqrt(dx*dx + dy*dy + dz*dz));
    CHECK(dist <= maxDistance);
}

void TestEnv::assertGrounded(PlayerId pid, bool grounded) {
    auto* pos = getPosition(pid);
    REQUIRE(pos != nullptr);
    CHECK(pos->grounded == grounded);
}

void TestEnv::assertInChunk(PlayerId pid, int32_t cx, int32_t cy, int32_t cz) {
    auto* pos = getPosition(pid);
    REQUIRE(pos != nullptr);
    ChunkId actual = ChunkId::fromSubVoxelPos(pos->x, pos->y, pos->z);
    ChunkId expected = ChunkId::make(cy, cx, cz);
    CHECK(actual == expected);
}

void TestEnv::assertVelocity(PlayerId pid, int32_t vx, int32_t vy, int32_t vz, int32_t tolerance) {
    auto* pos = getPosition(pid);
    REQUIRE(pos != nullptr);
    CHECK(std::abs(pos->vx - vx) <= tolerance);
    CHECK(std::abs(pos->vy - vy) <= tolerance);
    CHECK(std::abs(pos->vz - vz) <= tolerance);
}

std::vector<PlayerId> TestEnv::getAllPlayers() {
    std::vector<PlayerId> result;
    auto view = registry().view<PlayerComponent>();
    for (auto ent : view) {
        result.push_back(view.get<PlayerComponent>(ent).playerId);
    }
    return result;
}

void TestEnv::onGatewayOutput(GatewayId /*gw*/, const uint8_t* data, size_t size) {
    gatewayOutput_.insert(gatewayOutput_.end(), data, data + size);
}

void TestEnv::onPlayerOutput(PlayerId pid, const uint8_t* data, size_t size) {
    auto& buf = playerOutputs_[pid];
    buf.insert(buf.end(), data, data + size);
}

// ── PhysicsTestEnv Implementation ────────────────────────────────────────────

PhysicsTestEnv::PhysicsTestEnv(int32_t groundY) : groundY_(groundY) {
    // Create flat world chunks around origin
    for (int cx = -2; cx <= 2; ++cx) {
        for (int cz = -2; cz <= 2; ++cz) {
            for (int cy = -1; cy <= 1; ++cy) {
                auto* chunk = chunks.createOrGet(ChunkId::make(cy, cx, cz));
                if (!chunk) continue;
                
                // Fill with ground
                for (int vx = 0; vx < CHUNK_SIZE_X; ++vx) {
                    for (int vz = 0; vz < CHUNK_SIZE_Z; ++vz) {
                        for (int vy = 0; vy < CHUNK_SIZE_Y; ++vy) {
                            int worldY = cy * CHUNK_SIZE_Y + vy;
                            VoxelType type = (worldY <= groundY) ? VoxelTypes::STONE : VoxelTypes::AIR;
                            int idx = vy * CHUNK_SIZE_X * CHUNK_SIZE_Z + vx * CHUNK_SIZE_Z + vz;
                            chunk->world.voxels[idx] = type;
                        }
                    }
                }
                
                // Rebuild physics type cache after filling voxels
                chunk->world.rebuildPhysicTypeCache();
            }
        }
    }
}

entt::entity PhysicsTestEnv::spawnEntity(int32_t x, int32_t y, int32_t z, PhysicsMode mode) {
    auto ent = registry.create();
    GlobalEntityId gid = nextEntityId_++;
    
    registry.emplace<GlobalEntityIdComponent>(ent, gid);
    registry.emplace<DynamicPositionComponent>(ent, x, y, z, 0, 0, 0, false);
    registry.emplace<DirtyComponent>(ent);
    
    // Set bounding box based on mode
    int32_t hx = PlayerEntity::PLAYER_BBOX_HX, hy = PlayerEntity::PLAYER_BBOX_HY, hz = PlayerEntity::PLAYER_BBOX_HZ;
    if (mode == PhysicsMode::GHOST) {
        hx = hy = hz = 128;  // Smaller for ghosts
    }
    registry.emplace<BoundingBoxComponent>(ent, hx, hy, hz);
    registry.emplace<PhysicsModeComponent>(ent, mode);
    
    // Add chunk membership and to chunk
    ChunkId cid = ChunkId::fromSubVoxelPos(x, y, z);
    registry.emplace<ChunkMembershipComponent>(ent, cid);
    if (auto* chunk = chunks.getChunkMutable(cid)) {
        chunk->entities.insert(ent);
    }
    
    return ent;
}

void PhysicsTestEnv::tick(int n) {
    for (int i = 0; i < n; ++i) {
        PhysicsSystem::apply(registry, chunks);
        JumpSystem::apply(registry, tickCount_, PlayerEntity::PLAYER_JUMP_VY);
        ++tickCount_;
        updateEntityChunks(chunks, registry);
    }
}

DynamicPositionComponent* PhysicsTestEnv::getPosition(entt::entity ent) {
    return registry.try_get<DynamicPositionComponent>(ent);
}

void PhysicsTestEnv::setVelocity(entt::entity ent, int32_t vx, int32_t vy, int32_t vz) {
    auto* pos = registry.try_get<DynamicPositionComponent>(ent);
    if (pos) {
        pos->vx = vx;
        pos->vy = vy;
        pos->vz = vz;
    }
}

// ── Helper Functions ─────────────────────────────────────────────────────────

std::vector<std::pair<uint8_t, std::vector<uint8_t>>> parseBatch(
    const std::vector<uint8_t>& data) {
    
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> result;
    size_t i = 0;
    
    while (i + 3 <= data.size()) {
        uint8_t type = data[i];
        uint16_t size = *reinterpret_cast<const uint16_t*>(&data[i + 1]);
        
        if (i + 3 + size > data.size()) break;
        
        std::vector<uint8_t> payload(data.begin() + i + 3, data.begin() + i + 3 + size);
        result.emplace_back(type, std::move(payload));
        
        i += 3 + size;
    }
    
    return result;
}

std::vector<std::vector<uint8_t>> findMessagesOfType(
    const std::vector<std::pair<uint8_t, std::vector<uint8_t>>>& batch,
    ServerMessageType type) {
    
    std::vector<std::vector<uint8_t>> result;
    for (const auto& [msgType, payload] : batch) {
        if (msgType == static_cast<uint8_t>(type)) {
            result.push_back(payload);
        }
    }
    return result;
}

// ── Protocol Fixture Loaders ─────────────────────────────────────────────────

std::filesystem::path getFixturesDirectory() {
    // Try to find the fixtures directory relative to the test executable
    // Common locations:
    // 1. tests/protocol_fixtures/ (from build directory)
    // 2. ../tests/protocol_fixtures/ (if running from tests/server/)
    // 3. ../../tests/protocol_fixtures/ (if running from build/tests/)
    
    std::vector<std::filesystem::path> candidates = {
        "tests/protocol_fixtures",
        "../tests/protocol_fixtures",
        "../../tests/protocol_fixtures",
        "../../../tests/protocol_fixtures",
    };
    
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate)) {
            return std::filesystem::absolute(candidate);
        }
    }
    
    // Fallback: use source directory if available via environment
    if (const char* srcDir = std::getenv("VOXELMMO_SOURCE_DIR")) {
        std::filesystem::path p = std::filesystem::path(srcDir) / "tests" / "protocol_fixtures";
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    
    // Last resort: assume current directory
    return std::filesystem::absolute("tests/protocol_fixtures");
}

std::vector<uint8_t> loadHexFixture(const std::string& relativePath) {
    std::filesystem::path filepath = getFixturesDirectory() / relativePath;
    
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open fixture file: " + filepath.string());
    }
    
    std::vector<uint8_t> result;
    std::string line;
    
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        std::istringstream iss(line);
        std::string hexByte;
        
        while (iss >> hexByte) {
            // Skip comment start mid-line
            if (hexByte[0] == '#') break;
            
            // Parse hex byte
            if (hexByte.size() != 2) {
                throw std::runtime_error("Invalid hex byte in " + filepath.string() + ": " + hexByte);
            }
            
            char* endPtr;
            unsigned long value = std::strtoul(hexByte.c_str(), &endPtr, 16);
            if (*endPtr != '\0' || value > 0xFF) {
                throw std::runtime_error("Invalid hex byte in " + filepath.string() + ": " + hexByte);
            }
            
            result.push_back(static_cast<uint8_t>(value));
        }
    }
    
    return result;
}

} // namespace voxelmmo
