#include "game/GameEngine.hpp"
#include "game/systems/InputSystem.hpp"
#include "game/systems/DisconnectedPlayerSystem.hpp"

#include "game/components/SheepBehaviorComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
#include "common/NetworkProtocol.hpp"
#include "common/VoxelTypes.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/DisconnectedPlayerComponent.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/SheepEntity.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace voxelmmo {

GameEngine::GameEngine(uint32_t cliSeed, GeneratorType cliType, bool seedProvided,
                       std::optional<EntityType> testEntityType, const std::string& gameKey)
{
    // Register entity spawn implementations
    entityFactory.registerSpawnImpl(EntityType::GHOST_PLAYER, GhostPlayerEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::PLAYER, PlayerEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::SHEEP, SheepEntity::spawnImpl);
    
    // Initialize SaveSystem and load/create global state
    saveSystem_ = std::make_unique<SaveSystem>(gameKey);
    saveSystem_->loadOrCreateGlobalState(cliSeed, cliType);
    
    // Configure WorldGenerator with loaded parameters
    uint32_t effectiveSeed = seedProvided ? saveSystem_->getSeed() : generateRandomSeed();
    worldGenerator = WorldGenerator(effectiveSeed, saveSystem_->getGeneratorType(), testEntityType);
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
        }
        break;
    }

    case ClientMessageType::JOIN: {
        auto msg = NetworkProtocol::parseJoin(data, size);
        if (!msg) return;
        // Ignore if player already has an entity (already spawned).
        if (playerEntities.find(playerId) != playerEntities.end()) return;
        // Enqueue player creation request for processing in tick()
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
    
    // Broadcast to all gateways (all gateways receive the same data)
    if (serializer_.hasChunkData() && outputCallback) {
        for (auto& [gwId, gwInfo] : gateways) {
            outputCallback(gwId, serializer_.getChunkData(), serializer_.getChunkDataSize());
        }
    }
}

// ── Physics ───────────────────────────────────────────────────────────────

void GameEngine::stepPhysics() {
    PhysicsSystem::apply(registry, chunkRegistry);
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
    SheepAISystem::apply(registry, tick);
    stepPhysics();

    // Process disconnected players once per second (before chunk membership update)
    if (tick - lastDisconnectCheckTick >= TICK_RATE) {
        DisconnectedPlayerSystem::process(registry, playerEntities, tick);
        lastDisconnectCheckTick = tick;
    }

    // Unified chunk membership update: rebuilds chunk entity sets, handles movement,
    // updates watchingPlayers, and activates chunks
    auto membershipResult = ChunkMembershipSystem::update(
        gateways, playerEntities, chunkRegistry, registry, WATCH_RADIUS, ACTIVATION_RADIUS, worldGenerator, entityFactory, tick);

    // Unload unwatched chunks (save first, then unload from memory)
    ChunkMembershipSystem::unloadUnwatchedChunks(chunkRegistry, registry, saveSystem_.get());

    // Set DELETE_ENTITY delta type on entities marked for deletion
    // This must happen before serialization so the DELETE delta is sent
    setDeleteDeltaOnPendingDeletions();

    // Send state updates to clients (includes chunk snapshots with player entities)
    serializeChunks();

    // Send SELF_ENTITY messages to newly created players (after serializeChunks)
    // This ensures the chunk snapshot arrives first, so client knows the entity
    sendSelfEntityMessages();

    // Clear all dirty flags after serialization
    clearAllDirtyFlags();

    // Destroy any pending deletions
    // (their DELETE deltas have been serialized and sent above)
    auto pendingDeletionView = registry.view<PendingDeleteComponent>();
    registry.destroy(pendingDeletionView.begin(), pendingDeletionView.end());
}

// ── Pending Deletion Management ────────────────────────────────────────────

void GameEngine::setDeleteDeltaOnPendingDeletions() {
    // Find all entities marked with PendingDeleteComponent and set their delta type
    // This ensures Chunk.cpp will serialize them with DELETE_ENTITY delta type
    auto view = registry.view<PendingDeleteComponent, DirtyComponent>();
    for (auto ent : view) {
        auto& dirty = view.get<DirtyComponent>(ent);
        dirty.markForDeletion();
    }
}

// ── Self Entity Messages ──────────────────────────────────────────────────

void GameEngine::sendSelfEntityMessages() {
    if (!playerOutputCallback) return;

    const uint32_t tick = static_cast<uint32_t>(tickCount);

    // Find all player entities that were created this tick
    auto view = registry.view<DirtyComponent, PlayerComponent, GlobalEntityIdComponent>();
    for (auto ent : view) {
        auto& dirty = view.get<DirtyComponent>(ent);
        if (!dirty.isCreated()) continue;

        const auto& player = view.get<PlayerComponent>(ent);
        const auto& globalId = view.get<GlobalEntityIdComponent>(ent);

        // Build and send SELF_ENTITY message
        auto msg = NetworkProtocol::buildSelfEntityMessage(globalId.id, tick);
        playerOutputCallback(player.playerId, msg.data(), msg.size());

        // Clear delta type so SELF_ENTITY is not sent again on next tick
        // Other dirty flags are preserved for chunk serialization
        dirty.snapshotDeltaType = DeltaType::UPDATE_ENTITY;
    }
}

// ── Dirty Flags Clearing ───────────────────────────────────────────────────

void GameEngine::processPendingPlayerCreations() {
    for (const auto& req : pendingPlayerCreations_) {
        const auto* spawnPos = worldGenerator.getPlayerSpawnPos(chunkRegistry, entityFactory, ACTIVATION_RADIUS, saveSystem_.get());
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
        
        // Add player entity to its chunk
        const ChunkId chunkId = ChunkId::fromSubVoxelPos(spawnPos[0], spawnPos[1], spawnPos[2]);
        chunkRegistry.addPlayerEntity(chunkId, ent, req.playerId);
    }
    pendingPlayerCreations_.clear();
}

void GameEngine::processPendingVoxelDeletions() {
    for (const auto& del : pendingVoxelDeletions_) {
        // Find the chunk containing this voxel
        const ChunkId chunkId = ChunkId::fromVoxelPos(del.vx, del.vy, del.vz);
        Chunk* chunk = chunkRegistry.getChunkMutable(chunkId);
        
        if (!chunk) continue;  // Chunk not loaded, skip
        
        // Convert world voxel coordinates to local chunk coordinates
        const auto cx = (del.vx >> CHUNK_SHIFT_X);
        const auto cy = (del.vy >> CHUNK_SHIFT_Y);
        const auto cz = (del.vz >> CHUNK_SHIFT_Z);
        const uint32_t localX = static_cast<uint32_t>(del.vx - (cx << CHUNK_SHIFT_X));
        const uint32_t localY = static_cast<uint32_t>(del.vy - (cy << CHUNK_SHIFT_Y));
        const uint32_t localZ = static_cast<uint32_t>(del.vz - (cz << CHUNK_SHIFT_Z));
        
        // Set voxel to AIR (this also records the delta)
        chunk->world.setVoxel(localX, localY, localZ, VoxelTypes::AIR);
    }
    pendingVoxelDeletions_.clear();
}

void GameEngine::processPendingVoxelCreations() {
    for (const auto& create : pendingVoxelCreations_) {
        // Find the chunk containing this voxel
        const ChunkId chunkId = ChunkId::fromVoxelPos(create.vx, create.vy, create.vz);
        Chunk* chunk = chunkRegistry.getChunkMutable(chunkId);
        
        if (!chunk) continue;  // Chunk not loaded, skip
        
        // Convert world voxel coordinates to local chunk coordinates
        const auto cx = (create.vx >> CHUNK_SHIFT_X);
        const auto cy = (create.vy >> CHUNK_SHIFT_Y);
        const auto cz = (create.vz >> CHUNK_SHIFT_Z);
        const uint32_t localX = static_cast<uint32_t>(create.vx - (cx << CHUNK_SHIFT_X));
        const uint32_t localY = static_cast<uint32_t>(create.vy - (cy << CHUNK_SHIFT_Y));
        const uint32_t localZ = static_cast<uint32_t>(create.vz - (cz << CHUNK_SHIFT_Z));
        
        // Set voxel to the requested type (this also records the delta)
        chunk->world.setVoxel(localX, localY, localZ, create.voxelType);
    }
    pendingVoxelCreations_.clear();
}

void GameEngine::processPendingBulkVoxelDeletions() {
    for (const auto& bulk : pendingBulkVoxelDeletions_) {
        // Calculate bounds (inclusive)
        const int32_t minX = std::min(bulk.startX, bulk.endX);
        const int32_t maxX = std::max(bulk.startX, bulk.endX);
        const int32_t minY = std::min(bulk.startY, bulk.endY);
        const int32_t maxY = std::max(bulk.startY, bulk.endY);
        const int32_t minZ = std::min(bulk.startZ, bulk.endZ);
        const int32_t maxZ = std::max(bulk.startZ, bulk.endZ);
        
        // Expand bulk deletion into individual deletions
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
        // Calculate bounds (inclusive)
        const int32_t minX = std::min(bulk.startX, bulk.endX);
        const int32_t maxX = std::max(bulk.startX, bulk.endX);
        const int32_t minY = std::min(bulk.startY, bulk.endY);
        const int32_t maxY = std::max(bulk.startY, bulk.endY);
        const int32_t minZ = std::min(bulk.startZ, bulk.endZ);
        const int32_t maxZ = std::max(bulk.startZ, bulk.endZ);
        
        // Expand bulk creation into individual creations
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

void GameEngine::clearAllDirtyFlags() {
    // Clear voxel deltas for all chunks
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->world.clearDelta();
    }
    
    // Note: Entity dirty flags are cleared by ChunkSerializer::serializeAllChunks()
    // which calls Chunk::clearEntityDirtyFlags() based on what was serialized.
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
    
    // Shutdown sequence
    std::cout << "[GameEngine] Stopping game loop...\n";
    
    // Save active chunks (unwatched chunks already saved when unloaded)
    std::cout << "[GameEngine] Saving active chunks...\n";
    saveSystem_->saveActiveChunks(chunkRegistry);
    
    // Save global state
    // Note: We don't have direct access to globalState here, it's in main.cpp
    // The caller should save global state after run() returns
    
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
