#include "game/GameEngine.hpp"
#include "game/systems/InputSystem.hpp"

#include "game/components/SheepBehaviorComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
#include "common/MessageTypes.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/SheepEntity.hpp"
#include <cmath>
#include <iostream>

namespace voxelmmo {

GameEngine::GameEngine(uint32_t seed, bool seedProvided,
                       GeneratorType type, std::optional<EntityType> testEntityType)
    : worldGenerator(seedProvided ? seed : generateRandomSeed(), type, testEntityType)
{
    // Register entity spawn implementations
    entityFactory.registerSpawnImpl(EntityType::GHOST_PLAYER, GhostPlayerEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::PLAYER, PlayerEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::SHEEP, SheepEntity::spawnImpl);
    // generate initial chunks and player spawn point
    worldGenerator.init(chunkRegistry, entityFactory, ACTIVATION_RADIUS);
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

// ── Player input ──────────────────────────────────────────────────────────

void GameEngine::handlePlayerInput(PlayerId playerId, const uint8_t* data, size_t size) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (size < 1) return;

    switch (static_cast<ClientMessageType>(data[0])) {

    case ClientMessageType::INPUT: {
        auto msg = NetworkProtocol::parseInput(data, size);
        if (!msg) return;
        auto it = playerEntities.find(playerId);
        if (it == playerEntities.end()) return;
        auto& inp  = registry.get<InputComponent>(it->second);
        inp.buttons = msg->buttons;
        inp.yaw     = msg->yaw;
        inp.pitch   = msg->pitch;
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
    //for (PlayerId pid : it->second.players) removePlayer(pid);
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

void GameEngine::teleportPlayer(PlayerId playerId, int32_t sx, int32_t sy, int32_t sz) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return;
    const auto& dyn = registry.get<DynamicPositionComponent>(it->second);
    DynamicPositionComponent::modify(registry, it->second,
        sx, sy, sz, dyn.vx, dyn.vy, dyn.vz, dyn.grounded, /*dirty=*/true);
}

// void GameEngine::removePlayer(PlayerId playerId) {
//     std::lock_guard<std::recursive_mutex> lock(mtx_);
    
//     // Remove from all gateway player sets (in case player was pending)
//     for (auto& [gid, info] : gateways) {
//         info.players.erase(playerId);
//     }
    
//     auto it = playerEntities.find(playerId);
//     if (it == playerEntities.end()) return;
//     const entt::entity ent = it->second;

//     // Remove from watching radius immediately (so they stop receiving updates)
//     if (auto* dyn = registry.try_get<DynamicPositionComponent>(ent)) {
//         const int32_t ocx = dyn->x >> CHUNK_SHIFT_X;
//         const int32_t ocy = dyn->y >> CHUNK_SHIFT_Y;
//         const int32_t ocz = dyn->z >> CHUNK_SHIFT_Z;
//         for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
//         for (int32_t dy = -1; dy <= 1; ++dy)
//         for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
//             const ChunkId cid = ChunkId::make(ocy + dy, ocx + dx, ocz + dz);
//             if (Chunk* chunk = chunkRegistry.getChunkMutable(cid))
//                 chunk->watchingPlayers.erase(playerId);
//         }
//     }

//     // Mark for deferred deletion - ChunkMembershipSystem will handle cleanup and destroy
//     ChunkMembershipSystem::markForDeletion(registry, ent);
//     playerEntities.erase(it);
// }

// ── Serialisation helpers ─────────────────────────────────────────────────

void GameEngine::serializeChunks() {
    // Build/update each chunk's state once (future: parallelisable over chunks)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->updateState(registry, tick);
    }

    // Dispatch one batch per gateway
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            Chunk* chunk = chunkRegistry.getChunkMutable(cid);
            if (!chunk) continue;
            
            const uint32_t lastTick = gwInfo.lastStateTick[cid];
            
            // Ask the chunk what data to send for this gateway
            const uint8_t* data = nullptr;
            size_t length = 0;
            chunk->getDataToSend(lastTick, data, length);
            
            if (length > 0 && data != nullptr) {
                NetworkProtocol::appendToBatch(batchBuf, data, length);
            }

            gwInfo.lastStateTick[cid] = tick;
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }

}

// ── Physics ───────────────────────────────────────────────────────────────

void GameEngine::stepPhysics() {
    PhysicsSystem::apply(registry, chunkRegistry);
}

// ── Chunk lookup ──────────────────────────────────────────────────────────

const Chunk* GameEngine::chunkAt(int32_t px, int32_t py, int32_t pz) noexcept {
    return chunkRegistry.getChunk(chunkIdOf(px, py, pz));
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

    // Destroy any pending deletions from previous tick first
    // (their DELETE deltas have already been serialized and sent)
    auto pendingDeletionView = registry.view<PendingDeleteComponent>();
    registry.destroy(pendingDeletionView.begin(), pendingDeletionView.end());

    // Create any pending entities at the start of the tick
    createPendingEntities();

    // Process pending player creation requests
    processPendingPlayerCreations();

    InputSystem::apply(registry);
    SheepAISystem::apply(registry, tick);
    stepPhysics();

    // Phase A: Check chunk membership for moved entities
    // Updates chunk.leftEntities and chunk.presentPlayers, adds new entities to chunks
    ChunkMembershipSystem::checkChunkMembership(registry, chunkRegistry);

    // Phase B: Update watched chunks and generate/activate needed chunks
    // Updates gateway.watchedChunks and chunk.watchingPlayers, generates entities
    auto watchedResult = ChunkMembershipSystem::updateAndActivatePlayersWatchedChunks(
        gateways, playerEntities, chunkRegistry, registry, WATCH_RADIUS, ACTIVATION_RADIUS, worldGenerator, entityFactory, tick);

    // Send state updates to clients (includes chunk snapshots with player entities)
    serializeChunks();

    // Send SELF_ENTITY messages to newly created players (after serializeChunks)
    // This ensures the chunk snapshot arrives first, so client knows the entity
    sendSelfEntityMessages();

    // Clear all dirty flags after serialization
    clearAllDirtyFlags();
    
    // Entities in pendingDeletions will be destroyed at the start of next tick
    // This ensures their DELETE deltas have been sent
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

        // Clear CREATED_BIT so SELF_ENTITY is not sent again on next tick
        // Other dirty flags are preserved for chunk serialization
        dirty.snapshotDirtyFlags &= ~DirtyComponent::CREATED_BIT;
    }
}

// ── Dirty Flags Clearing ───────────────────────────────────────────────────

void GameEngine::processPendingPlayerCreations() {
    for (const auto& req : pendingPlayerCreations_) {
        const auto* spawnPos = worldGenerator.getPlayerSpawnPos();
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
        const ChunkId chunkId = chunkIdOf(spawnPos[0], spawnPos[1], spawnPos[2]);
        chunkRegistry.addPlayerEntity(chunkId, ent, req.playerId);
    }
    pendingPlayerCreations_.clear();
}

void GameEngine::clearAllDirtyFlags() {
    // Clear chunk-level state
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->state.hasNewData = false;
        chunkPtr->world.clearSnapshotDelta();
        chunkPtr->world.clearTickDelta();
    }

    // Clear entity dirty flags for all entities in registry
    auto view = registry.view<DirtyComponent>();
    for (auto ent : view) {
        auto& dirty = view.get<DirtyComponent>(ent);
        dirty.clearSnapshot();
        dirty.clearTick();
    }
}

} // namespace voxelmmo
