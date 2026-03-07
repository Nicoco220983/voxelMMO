#include "game/GameEngine.hpp"
#include "game/systems/InputSystem.hpp"

#include "game/components/SheepBehaviorComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/PendingDeleteComponent.hpp"
#include "common/MessageTypes.hpp"
#include "game/components/PlayerComponent.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/SheepEntity.hpp"
#include <cmath>

namespace voxelmmo {

GameEngine::GameEngine(uint32_t seed, bool seedProvided,
                       GeneratorType type, EntityType testEntityType)
    : worldGenerator(seedProvided ? seed : generateRandomSeed(), type, testEntityType)
{
    // Register entity spawn implementations
    entityFactory.registerSpawnImpl(EntityType::GHOST_PLAYER, GhostPlayerEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::PLAYER, PlayerEntity::spawnImpl);
    entityFactory.registerSpawnImpl(EntityType::SHEEP, SheepEntity::spawnImpl);

    // Generate initial chunks around (0,0,0) and compute player spawn position
    // This is done here rather than in WorldGenerator constructor because
    // chunkRegistry and registry must be initialized first.
    worldGenerator.generateChunks(chunkRegistry, 0, 0, 0, ACTIVATION_RADIUS, entityFactory, 0);
    worldGenerator.computePlayerSpawnPos();
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
        // Must be a pending player (not yet spawned).
        auto pit = pendingPlayers.find(playerId);
        if (pit == pendingPlayers.end()) return;
        const PendingPlayer p = pit->second;
        pendingPlayers.erase(pit);
        addPlayer(p.gwId, playerId, p.sx, p.sy, p.sz, msg->entityType);
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
    for (PlayerId pid : it->second.players) removePlayer(pid);
    gateways.erase(it);
}

// ── Player management ─────────────────────────────────────────────────────

void GameEngine::queuePendingPlayer(GatewayId gwId, PlayerId playerId,
                                     int32_t sx, int32_t sy, int32_t sz)
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    pendingPlayers[playerId] = {gwId, sx, sy, sz};
    if (auto it = gateways.find(gwId); it != gateways.end())
        it->second.players.insert(playerId);
}

void GameEngine::addPlayer(GatewayId gwId, PlayerId playerId,
                            int32_t /*sx*/, int32_t /*sy*/, int32_t /*sz*/, EntityType type)
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);

    // Queue player spawn at world generator's spawn position
    worldGenerator.addPlayer(entityFactory, type, playerId);

    // Create all pending entities (including the player)
    auto created = createPendingEntities();
    if (created.empty()) return;

    // The last created entity should be our player
    const entt::entity ent = created.back();
    playerEntities[playerId] = ent;

    if (auto it = gateways.find(gwId); it != gateways.end())
        it->second.players.insert(playerId);

    // Note: Chunk generation and SELF_ENTITY message are handled later in the flow
    // by rebuildGatewayWatchedChunks() and updateEntitiesChunks() respectively.
}

void GameEngine::teleportPlayer(PlayerId playerId, int32_t sx, int32_t sy, int32_t sz) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return;
    const auto& dyn = registry.get<DynamicPositionComponent>(it->second);
    DynamicPositionComponent::modify(registry, it->second,
        sx, sy, sz, dyn.vx, dyn.vy, dyn.vz, dyn.grounded, /*dirty=*/true);
}

void GameEngine::removePlayer(PlayerId playerId) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return;
    const entt::entity ent = it->second;

    // Remove from watching radius immediately (so they stop receiving updates)
    if (auto* dyn = registry.try_get<DynamicPositionComponent>(ent)) {
        const int32_t ocx = dyn->x >> CHUNK_SHIFT_X;
        const int32_t ocy = dyn->y >> CHUNK_SHIFT_Y;
        const int32_t ocz = dyn->z >> CHUNK_SHIFT_Z;
        for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
        for (int32_t dy = -1; dy <= 1; ++dy)
        for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
            const ChunkId cid = ChunkId::make(ocy + dy, ocx + dx, ocz + dz);
            if (Chunk* chunk = chunkRegistry.getChunkMutable(cid))
                chunk->watchingPlayers.erase(playerId);
        }
    }

    // Mark for deferred deletion - ChunkMembershipSystem will handle cleanup and destroy
    ChunkMembershipSystem::markForDeletion(registry, ent);
    playerEntities.erase(it);
}



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
            const auto& state = chunk->state;
            const uint32_t lastTick = gwInfo.lastStateTick[cid];

            if (lastTick == 0) {
                // New watcher: send full snapshot
                NetworkProtocol::appendToBatch(batchBuf, state.snapshot);
                gwInfo.lastStateTick[cid] = state.snapshotTick;
            } else if (state.hasNewDelta) {
                // Existing watcher with new data: send latest delta
                const size_t off = state.deltaOffsets.back().offset;
                NetworkProtocol::appendToBatch(batchBuf, state.deltas.data() + off, state.deltas.size() - off);
                gwInfo.lastStateTick[cid] = state.deltaOffsets.back().tick;
            }
            // else: no new data for this watcher, skip
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }

    // Clear both snapshot-level and tick-level dirty state and hasNewDelta flags
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->state.hasNewDelta = false;
        chunkPtr->world.clearSnapshotDelta();
        chunkPtr->world.clearTickDelta();
        for (auto ent : chunkPtr->entities) {
            auto& dirty = registry.get<DirtyComponent>(ent);
            dirty.clearSnapshot();
            dirty.clearTick();
        }
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

std::vector<entt::entity> GameEngine::createPendingEntities() {
    auto acquireId = [this]() { return acquireEntityId(); };
    return entityFactory.createEntities(registry, acquireId);
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

    InputSystem::apply(registry);
    SheepAISystem::apply(registry, tick);
    stepPhysics();

    // Phase A: Check chunk membership for moved entities
    // Updates chunk.leftEntities and chunk.presentPlayers
    ChunkMembershipSystem::checkChunkMembership(registry, chunkRegistry);

    // Phase B: Update watched chunks and generate/activate needed chunks
    // Updates gateway.watchedChunks and chunk.watchingPlayers, generates entities
    auto watchedResult = ChunkMembershipSystem::updateAndActivatePlayersWatchedChunks(
        gateways, playerEntities, chunkRegistry, registry, WATCH_RADIUS, ACTIVATION_RADIUS, worldGenerator, entityFactory, tick);

    // Send state updates to clients
    serializeChunks();
    
    // Entities in pendingDeletions will be destroyed at the start of next tick
    // This ensures their DELETE deltas have been sent
}

} // namespace voxelmmo
