#include "game/GameEngine.hpp"
#include "game/WorldGenerator.hpp"
#include "game/SaveSystem.hpp"
#include "game/Chunk.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/SheepEntity.hpp"
#include "game/entities/GoblinEntity.hpp"
#include "game/systems/InputSystem.hpp"
#include "game/systems/PhysicsSystem.hpp"
#include "game/systems/JumpSystem.hpp"
#include "game/systems/ChunkMembershipSystem.hpp"
#include "game/systems/SheepAISystem.hpp"
#include "game/systems/GoblinAISystem.hpp"
#include "game/systems/HealthSystem.hpp"
#include "game/systems/DisconnectedPlayerSystem.hpp"
#include "game/systems/CombatSystem.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/ChunkMembershipComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/DisconnectedPlayerComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "game/components/HealthComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/ToolComponent.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/VoxelTypes.hpp"
#include "common/EntityType.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <chrono>
#include <thread>

namespace voxelmmo {

GameEngine::GameEngine(uint32_t cliSeed, GeneratorType cliType, bool seedProvided,
                       std::optional<EntityType> testEntityType, const std::string& gameKey)
{
    // Register entity spawn implementations
    entityFactory.registerSpawnImpl(EntityType::GHOST_PLAYER, GhostPlayerEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::PLAYER, PlayerEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::SHEEP, SheepEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::GOBLIN, GoblinEntity::spawnImpl);
    
    // Initialize SaveSystem and load/create global state
    saveSystem_ = std::make_unique<SaveSystem>(gameKey);
    saveSystem_->loadOrCreateGlobalState(cliSeed, cliType);
    
    // Configure WorldGenerator with loaded parameters
    uint32_t effectiveSeed = seedProvided ? saveSystem_->getSeed() : generateRandomSeed();
    worldGenerator = std::make_unique<WorldGenerator>(effectiveSeed, saveSystem_->getGeneratorType(), testEntityType);
}

uint32_t GameEngine::generateRandomSeed() {
    std::random_device rd;
    std::mt19937_64 rng(rd());
    std::uniform_int_distribution<uint32_t> dist;
    return dist(rng);
}

void GameEngine::setOutputCallback(OutputCallback cb) {
    outputCallback = std::move(cb);
}

void GameEngine::setPlayerOutputCallback(PlayerOutputCallback cb) {
    playerOutputCallback = std::move(cb);
}

void GameEngine::saveActiveChunks() {
    if (saveSystem_) {
        saveSystem_->saveActiveChunks(chunkRegistry);
    }
}

void GameEngine::saveGlobalState() {
    if (saveSystem_) {
        saveSystem_->saveGlobalState();
    }
}

SaveSystem* GameEngine::getSaveSystem() const {
    return saveSystem_.get();
}

std::string GameEngine::getSaveDirectory() const {
    return saveSystem_ ? saveSystem_->getBaseDir() : "";
}

uint32_t GameEngine::getSeed() const {
    return saveSystem_ ? saveSystem_->getSeed() : 0;
}

GeneratorType GameEngine::getGeneratorType() const {
    return saveSystem_ ? saveSystem_->getGeneratorType() : GeneratorType::NORMAL;
}

const WorldGenerator& GameEngine::getWorldGenerator() const {
    return *worldGenerator;
}

WorldGenerator& GameEngine::getWorldGenerator() {
    return *worldGenerator;
}

// ── Player input ──────────────────────────────────────────────────────────

void GameEngine::handlePlayerInput(PlayerId playerId, const uint8_t* data, size_t size) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (size < 1) return;

    switch (static_cast<ClientMessageType>(data[0])) {

    case ClientMessageType::INPUT: {
        auto msg = NetworkProtocol::parseInput(data, size);
        if (!msg) return;
        
        switch (msg->inputType) {
            case InputType::MOVE: {
                auto it = playerEntities.find(playerId);
                if (it == playerEntities.end()) return;
                auto& inp  = registry.get<InputComponent>(it->second);
                inp.buttons = msg->buttons;
                inp.yaw     = msg->yaw;
                inp.pitch   = msg->pitch;
                break;
            }
            case InputType::VOXEL_DESTROY: {
                // Enqueue voxel deletion request for processing in tick()
                pendingVoxelDeletions_.push_back({msg->vx, msg->vy, msg->vz});
                break;
            }
            case InputType::VOXEL_CREATE: {
                // Enqueue voxel creation request for processing in tick()
                pendingVoxelCreations_.push_back({msg->vx, msg->vy, msg->vz, msg->voxelType});
                break;
            }
            case InputType::BULK_VOXEL_DESTROY: {
                // Enqueue bulk voxel deletion request for processing in tick()
                pendingBulkVoxelDeletions_.push_back({
                    msg->startX, msg->startY, msg->startZ,
                    msg->endX, msg->endY, msg->endZ
                });
                break;
            }
            case InputType::BULK_VOXEL_CREATE: {
                // Enqueue bulk voxel creation request for processing in tick()
                pendingBulkVoxelCreations_.push_back({
                    msg->startX, msg->startY, msg->startZ,
                    msg->endX, msg->endY, msg->endZ,
                    msg->voxelType
                });
                break;
            }
            case InputType::TOOL_USE: {
                // Enqueue tool use request for processing in tick()
                pendingToolUses_.push_back({playerId, msg->toolId, msg->yaw, msg->pitch});
                break;
            }
            case InputType::TOOL_SELECT: {
                // Enqueue tool selection request for processing in tick()
                pendingToolSelects_.push_back({playerId, msg->toolId});
                break;
            }
        }
        break;
    }

    case ClientMessageType::JOIN: {
        auto msg = NetworkProtocol::parseJoin(data, size);
        if (!msg) return;
        // Enqueue player creation request for processing in tick()
        // (Reconnection check happens in processPendingPlayerCreations)
        pendingPlayerCreations_.push_back({playerId, msg->entityType});
        break;
    }

    } // switch
}

// ── Gateway management ────────────────────────────────────────────────────

void GameEngine::registerGateway(GatewayId gwId) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    gateways.emplace(gwId, GatewayInfo{});
}

void GameEngine::unregisterGateway(GatewayId gwId) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = gateways.find(gwId);
    if (it == gateways.end()) return;
    gateways.erase(it);
}

// ── Player management ─────────────────────────────────────────────────────

void GameEngine::registerPlayer(GatewayId gwId, PlayerId playerId)
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    // Player is tracked as "pending" (no entity yet) until JOIN message arrives.
    // Presence in gateways[gwId].players but not in playerEntities = pending.
    if (auto it = gateways.find(gwId); it != gateways.end())
        it->second.players.insert(playerId);
}

void GameEngine::teleportPlayer(PlayerId playerId, SubVoxelCoord sx, SubVoxelCoord sy, SubVoxelCoord sz) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return;
    const auto& dyn = registry.get<DynamicPositionComponent>(it->second);
    DynamicPositionComponent::modify(registry, it->second,
        sx, sy, sz, dyn.vx, dyn.vy, dyn.vz, dyn.grounded, /*dirty=*/true);
}

void GameEngine::markPlayerDisconnected(PlayerId playerId) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return;

    entt::entity ent = it->second;

    // Avoid double-marking
    if (registry.all_of<DisconnectedPlayerComponent>(ent)) {
        return;
    }

    // Add the disconnected component with current tick
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    registry.emplace<DisconnectedPlayerComponent>(ent, tick);
}

bool GameEngine::cancelPlayerDisconnection(PlayerId playerId) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return false;

    return DisconnectedPlayerSystem::cancelDisconnection(registry, it->second);
}

// ── Serialisation helpers ─────────────────────────────────────────────────

void GameEngine::serializeChunks() {
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    
    // Clear and serialize all chunks into the concatenated buffer
    serializer_.clear();
    serializer_.serializeAllChunks(registry, chunkRegistry, tick);
    
    // Broadcast chunk data to all gateways (all gateways receive the same data)
    if (serializer_.hasChunkData() && outputCallback) {
        for (auto& [gwId, gwInfo] : gateways) {
            outputCallback(gwId, serializer_.getChunkData(), serializer_.getChunkDataSize());
        }
    }
    
    // Send buffered SELF_ENTITY messages to individual players
    if (serializer_.hasSelfEntityData() && playerOutputCallback) {
        for (const auto& msg : serializer_.getSelfEntityMessages()) {
            playerOutputCallback(msg.playerId, msg.data.data(), msg.data.size());
        }
    }
}

// ── Chunk lookup ──────────────────────────────────────────────────────────

const Chunk* GameEngine::chunkAt(SubVoxelCoord px, SubVoxelCoord py, SubVoxelCoord pz) noexcept {
    return chunkRegistry.getChunk(ChunkId::fromSubVoxelPos(px, py, pz));
}

// ── Main tick ─────────────────────────────────────────────────────────────

void GameEngine::createPendingEntities() {
    auto acquireId = [this]() { return acquireEntityId(); };
    entityFactory.createEntities(registry, chunkRegistry, acquireId);
}

void GameEngine::tick() {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    ++tickCount;
    const uint32_t tick = static_cast<uint32_t>(tickCount);

    // Create any pending entities at the start of the tick
    createPendingEntities();
    // Process pending player creation requests
    processPendingPlayerCreations();
    // Process pending voxel deletions and creations
    // Process bulk operations first (they expand into individual operations)
    processPendingBulkVoxelDeletions();
    processPendingBulkVoxelCreations();
    // Then process all individual operations (including expanded bulk)
    processPendingVoxelDeletions();
    processPendingVoxelCreations();

    InputSystem::apply(registry);
    
    // Process tool selections (authoritative - server updates ToolComponent)
    processPendingToolSelects();
    
    // Process tool uses (combat)
    processPendingToolUses();
    
    SheepAISystem::apply(registry, tick);
    GoblinAISystem::apply(registry, tick);
    PhysicsSystem::apply(registry, chunkRegistry, static_cast<uint32_t>(tickCount));
    JumpSystem::apply(registry, static_cast<uint32_t>(tickCount), PlayerEntity::PLAYER_JUMP_VY);

    // Process disconnected players once per second (before chunk membership update)
    if (tick - lastDisconnectCheckTick >= TICK_RATE) {
        DisconnectedPlayerSystem::process(registry, playerEntities, tick);
        lastDisconnectCheckTick = tick;
    }

    // Check health-based deletion timeouts (sets DELETE_ENTITY delta type)
    HealthSystem::processDeathTimeouts(registry, tick);

    // Unified chunk membership update: rebuilds chunk entity sets, handles movement,
    // updates watchingPlayers, and activates chunks
    auto membershipResult = ChunkMembershipSystem::update(
        gateways, playerEntities, chunkRegistry, registry, WATCH_RADIUS, ACTIVATION_RADIUS, 
        *worldGenerator, entityFactory, tick, saveSystem_.get());

    // Unload unwatched chunks (save first, then unload from memory)
    ChunkMembershipSystem::unloadUnwatchedChunks(chunkRegistry, registry, saveSystem_.get());

    // Send state updates to clients (includes chunk snapshots with player entities)
    // Also sends SELF_ENTITY messages to newly created players
    // Also clear dirty flags
    serializeChunks();
    
    // Destroy TTL-expired entities (after their DELETE delta has been serialized)
    processPendingDeletions(tick);
}

// ── Pending Deletion Management ────────────────────────────────────────────

void GameEngine::processPendingDeletions(uint32_t tick) {
    // Destroy entities marked for deletion.
    // This runs AFTER serialization so DELETE deltas have been sent to clients.
    std::vector<entt::entity> toDestroy;
    
    {
        auto view = registry.view<DirtyComponent>();
        for (auto ent : view) {
            const auto& dirty = view.get<DirtyComponent>(ent);
            if (dirty.deltaType == DeltaType::DELETE_ENTITY) {
                if (auto* health = registry.try_get<HealthComponent>(ent)) {
                    if (!health->shouldDelete(tick)) {
                        continue;
                    }
                }
                toDestroy.push_back(ent);
            }
        }
    }
    
    // Remove from playerEntities map and chunk entity sets before destroying
    for (auto ent : toDestroy) {
        // Remove from playerEntities map
        for (auto it = playerEntities.begin(); it != playerEntities.end(); ) {
            if (it->second == ent) {
                it = playerEntities.erase(it);
            } else {
                ++it;
            }
        }
        
        // Remove from chunk entity set
        if (auto* membership = registry.try_get<ChunkMembershipComponent>(ent)) {
            PlayerId playerId = 0;
            if (auto* playerComp = registry.try_get<PlayerComponent>(ent)) {
                playerId = playerComp->playerId;
            }
            chunkRegistry.removeEntity(membership->currentChunkId, ent, playerId);
        }
    }
    
    registry.destroy(toDestroy.begin(), toDestroy.end());
}

void GameEngine::processPendingPlayerCreations() {
    for (const auto& req : pendingPlayerCreations_) {
        auto existingIt = playerEntities.find(req.playerId);
        
        if (existingIt != playerEntities.end()) {
            entt::entity ent = existingIt->second;
            
            if (registry.valid(ent)) {
                if (auto* health = registry.try_get<HealthComponent>(ent)) {
                    if (health->deleteAtTick > 0) {
                        health->deleteAtTick = 0;
                        registry.remove<PlayerComponent>(ent);
                    } else {
                        if (registry.all_of<DisconnectedPlayerComponent>(ent)) {
                            registry.remove<DisconnectedPlayerComponent>(ent);
                        }

                        auto& dirty = registry.get<DirtyComponent>(ent);
                        dirty.markCreated();
                        
                        auto& pos = registry.get<DynamicPositionComponent>(ent);
                        const ChunkId chunkId = ChunkId::fromSubVoxelPos(pos.x, pos.y, pos.z);
                        chunkRegistry.addPlayerEntity(chunkId, ent, req.playerId);
                        
                        std::cout << "[GameEngine] Client " << req.playerId << " connected (reconnect)\n";
                        continue;
                    }
                }
            }
        }
        
        const auto* spawnPos = worldGenerator->getPlayerSpawnPos(chunkRegistry, entityFactory, ACTIVATION_RADIUS, saveSystem_.get());
        const GlobalEntityId globalId = acquireEntityId();
        
        entt::entity ent;
        switch (req.entityType) {
            case EntityType::PLAYER:
                ent = PlayerEntity::spawn(registry, globalId, spawnPos[0], spawnPos[1], spawnPos[2], req.playerId);
                break;
            case EntityType::GHOST_PLAYER:
            default:
                ent = GhostPlayerEntity::spawn(registry, globalId, spawnPos[0], spawnPos[1], spawnPos[2], req.playerId);
                break;
        }
        
        playerEntities[req.playerId] = ent;
        
        const ChunkId chunkId = ChunkId::fromSubVoxelPos(spawnPos[0], spawnPos[1], spawnPos[2]);
        chunkRegistry.addPlayerEntity(chunkId, ent, req.playerId);
        
        std::cout << "[GameEngine] Client " << req.playerId << " connected (new)\n";
    }
    pendingPlayerCreations_.clear();
}

void GameEngine::processPendingVoxelDeletions() {
    for (const auto& del : pendingVoxelDeletions_) {
        const ChunkId chunkId = ChunkId::fromVoxelPos(del.vx, del.vy, del.vz);
        Chunk* chunk = chunkRegistry.getChunkMutable(chunkId);
        
        if (!chunk) continue;
        
        const auto cx = (del.vx >> CHUNK_SHIFT_X);
        const auto cy = (del.vy >> CHUNK_SHIFT_Y);
        const auto cz = (del.vz >> CHUNK_SHIFT_Z);
        const uint32_t localX = static_cast<uint32_t>(del.vx - (cx << CHUNK_SHIFT_X));
        const uint32_t localY = static_cast<uint32_t>(del.vy - (cy << CHUNK_SHIFT_Y));
        const uint32_t localZ = static_cast<uint32_t>(del.vz - (cz << CHUNK_SHIFT_Z));
        
        chunk->world.setVoxel(localX, localY, localZ, VoxelTypes::AIR);
    }
    pendingVoxelDeletions_.clear();
}

void GameEngine::processPendingVoxelCreations() {
    for (const auto& create : pendingVoxelCreations_) {
        const ChunkId chunkId = ChunkId::fromVoxelPos(create.vx, create.vy, create.vz);
        Chunk* chunk = chunkRegistry.getChunkMutable(chunkId);
        
        if (!chunk) continue;
        
        const auto cx = (create.vx >> CHUNK_SHIFT_X);
        const auto cy = (create.vy >> CHUNK_SHIFT_Y);
        const auto cz = (create.vz >> CHUNK_SHIFT_Z);
        const uint32_t localX = static_cast<uint32_t>(create.vx - (cx << CHUNK_SHIFT_X));
        const uint32_t localY = static_cast<uint32_t>(create.vy - (cy << CHUNK_SHIFT_Y));
        const uint32_t localZ = static_cast<uint32_t>(create.vz - (cz << CHUNK_SHIFT_Z));
        
        chunk->world.setVoxel(localX, localY, localZ, create.voxelType);
    }
    pendingVoxelCreations_.clear();
}

void GameEngine::processPendingBulkVoxelDeletions() {
    for (const auto& bulk : pendingBulkVoxelDeletions_) {
        const int32_t minX = std::min(bulk.startX, bulk.endX);
        const int32_t maxX = std::max(bulk.startX, bulk.endX);
        const int32_t minY = std::min(bulk.startY, bulk.endY);
        const int32_t maxY = std::max(bulk.startY, bulk.endY);
        const int32_t minZ = std::min(bulk.startZ, bulk.endZ);
        const int32_t maxZ = std::max(bulk.startZ, bulk.endZ);
        
        for (int32_t x = minX; x <= maxX; ++x) {
            for (int32_t y = minY; y <= maxY; ++y) {
                for (int32_t z = minZ; z <= maxZ; ++z) {
                    pendingVoxelDeletions_.push_back({x, y, z});
                }
            }
        }
    }
    pendingBulkVoxelDeletions_.clear();
}

void GameEngine::processPendingBulkVoxelCreations() {
    for (const auto& bulk : pendingBulkVoxelCreations_) {
        const int32_t minX = std::min(bulk.startX, bulk.endX);
        const int32_t maxX = std::max(bulk.startX, bulk.endX);
        const int32_t minY = std::min(bulk.startY, bulk.endY);
        const int32_t maxY = std::max(bulk.startY, bulk.endY);
        const int32_t minZ = std::min(bulk.startZ, bulk.endZ);
        const int32_t maxZ = std::max(bulk.startZ, bulk.endZ);
        
        for (int32_t x = minX; x <= maxX; ++x) {
            for (int32_t y = minY; y <= maxY; ++y) {
                for (int32_t z = minZ; z <= maxZ; ++z) {
                    pendingVoxelCreations_.push_back({x, y, z, bulk.voxelType});
                }
            }
        }
    }
    pendingBulkVoxelCreations_.clear();
}

void GameEngine::processPendingToolSelects() {
    for (const auto& select : pendingToolSelects_) {
        auto it = playerEntities.find(select.playerId);
        if (it == playerEntities.end()) continue;
        
        entt::entity ent = it->second;
        if (!registry.valid(ent)) continue;
        
        // Ensure entity has ToolComponent
        if (!registry.try_get<ToolComponent>(ent)) {
            registry.emplace<ToolComponent>(ent);
        }
        
        // Update tool (dirty flag will send via chunk delta)
        ToolComponent::modify(registry, ent, select.toolId, /*dirty=*/true);
    }
    pendingToolSelects_.clear();
}

void GameEngine::processPendingToolUses() {
    for (const auto& use : pendingToolUses_) {
        CombatSystem::processToolUse(registry, playerEntities,
                                     use.playerId, use.toolId,
                                     use.yaw, use.pitch,
                                     static_cast<uint32_t>(tickCount));
    }
    pendingToolUses_.clear();
}

void GameEngine::sendSnapshot(GatewayId gwId) {
    (void)gwId;
}

// ── Game loop lifecycle ────────────────────────────────────────────────────

void GameEngine::run() {
    if (running_.exchange(true)) {
        std::cerr << "[GameEngine] Error: run() called but already running\n";
        return;
    }
    
    if (!saveSystem_) {
        std::cerr << "[GameEngine] Error: SaveSystem not set, cannot run\n";
        running_ = false;
        return;
    }
    
    std::cout << "[GameEngine] Starting game loop...\n";
    stopRequested_ = false;
    
    using Clock = std::chrono::steady_clock;
    const auto tickDuration = std::chrono::milliseconds(1000 / TICK_RATE);
    auto nextTick = Clock::now();

    while (!stopRequested_.load()) {
        nextTick += tickDuration;
        tick();
        std::this_thread::sleep_until(nextTick);
    }
    
    std::cout << "[GameEngine] Stopping game loop...\n";
    std::cout << "[GameEngine] Saving active chunks...\n";
    saveSystem_->saveActiveChunks(chunkRegistry);
    
    std::cout << "[GameEngine] Game loop stopped\n";
    running_ = false;
}

void GameEngine::stop() {
    if (!running_.load()) {
        std::cout << "[GameEngine] stop() called but not running\n";
        return;
    }
    
    std::cout << "[GameEngine] Shutdown requested\n";
    stopRequested_ = true;
}

} // namespace voxelmmo
