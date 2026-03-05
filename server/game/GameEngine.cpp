#include "game/GameEngine.hpp"
#include "game/systems/InputSystem.hpp"

#include "game/components/SheepBehaviorComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "common/MessageTypes.hpp"
#include "game/components/PlayerComponent.hpp"
#include <cmath>

namespace voxelmmo {

GameEngine::GameEngine(uint32_t seed, bool seedProvided,
                       GeneratorType type, EntityType testEntityType)
    : worldGenerator(seedProvided ? seed : generateRandomSeed(), type, testEntityType)
{
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
        sendSnapshot(p.gwId);
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
                            int32_t sx, int32_t sy, int32_t sz, EntityType type)
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    const auto fit = playerFactories.find(type);
    if (fit == playerFactories.end()) return;

    // Compute initial chunk from spawn position
    const int32_t cx = sx >> CHUNK_SHIFT_X;
    const int32_t cy = sy >> CHUNK_SHIFT_Y;
    const int32_t cz = sz >> CHUNK_SHIFT_Z;
    const ChunkId chunkId = ChunkId::make(cy, cx, cz);

    const entt::entity ent = registry.create();

    // Assign stable global entity ID (persists across chunk moves)
    const GlobalEntityId globalId = acquireEntityId();
    registry.emplace<GlobalEntityIdComponent>(ent, globalId);

    // Ensure DirtyComponent exists for lifecycle tracking
    registry.emplace<DirtyComponent>(ent);

    fit->second(registry, ent, sx, sy, sz, playerId, chunkId);
    playerEntities[playerId] = ent;

    // Mark for creation - ChunkMembershipSystem will add to chunk during tick
    ChunkMembershipSystem::markForCreation(registry, ent, chunkId);

    // Set player spawn position for TEST mode entity spawning
    worldGenerator.setPlayerSpawnPos(sx, sy, sz);
    
    // Add to watching radius (generate chunks as needed)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
    for (int32_t dy = -1; dy <= 1; ++dy)
    for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
        const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
        Chunk* chunk = chunkRegistry.generate(worldGenerator, cid, registry, tick);
        chunk->watchingPlayers.insert(playerId);
    }

    if (auto it = gateways.find(gwId); it != gateways.end())
        it->second.players.insert(playerId);

    // Send SELF_ENTITY message once at creation (global ID is stable across chunk moves)
    std::vector<uint8_t> selfEntityBuf;
    const auto msg = NetworkProtocol::buildSelfEntityMessage(chunkId, tick, globalId);
    NetworkProtocol::appendFramed(selfEntityBuf, msg.data(), msg.size());
    if (!selfEntityBuf.empty() && outputCallback)
        outputCallback(gwId, selfEntityBuf.data(), selfEntityBuf.size());
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
    if (registry.all_of<ChunkMembershipComponent>(ent)) {
        const auto& cm = registry.get<ChunkMembershipComponent>(ent);
        const int32_t ocx = cm.currentChunkId.x();
        const int32_t ocy = cm.currentChunkId.y();
        const int32_t ocz = cm.currentChunkId.z();
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

void GameEngine::sendSnapshot(GatewayId gwId) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    // watchedChunks is populated by ChunkMembershipSystem; run it now so a
    // snapshot requested immediately after addPlayer() is not empty.
    auto it = gateways.find(gwId);
    if (it == gateways.end()) return;
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    batchBuf = ChunkMembershipSystem::rebuildGatewayWatchedChunks(
        it->second, chunkRegistry, playerEntities, registry, tick, WATCH_RADIUS, ACTIVATION_RADIUS, worldGenerator);
    ChunkMembershipSystem::detectChunkCrossings(registry, chunkRegistry, tickCount, ACTIVATION_RADIUS);
    if (!batchBuf.empty() && outputCallback)
        outputCallback(gwId, batchBuf.data(), batchBuf.size());
    serializeSnapshot(gwId);
}

void GameEngine::serializeSnapshot(GatewayId gwId) {
    auto it = gateways.find(gwId);
    if (it == gateways.end()) return;
    auto& gwInfo = it->second;
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    batchBuf.clear();
    for (const ChunkId& cid : gwInfo.watchedChunks) {
        Chunk* chunk = chunkRegistry.getChunkMutable(cid);
        if (!chunk) continue;
        NetworkProtocol::appendFramed(batchBuf, chunk->buildSnapshot(registry, tick));
        gwInfo.lastStateTick[cid] = tick;
    }
    if (!batchBuf.empty() && outputCallback)
        outputCallback(gwId, batchBuf.data(), batchBuf.size());
}

void GameEngine::serializeSnapshotDelta() {
    // Build each chunk's delta once (future: parallelisable over chunks)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->buildSnapshotDelta(registry, tick);
    }
    // Dispatch one batch per gateway (only chunks that produced a new delta)
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            Chunk* chunk = chunkRegistry.getChunkMutable(cid);
            if (!chunk) continue;
            const auto& state = chunk->state;
            if (!state.hasNewDelta) continue;
            const size_t off = state.deltaOffsets.back().offset;
            NetworkProtocol::appendFramed(batchBuf, state.deltas.data() + off, state.deltas.size() - off);
            gwInfo.lastStateTick[cid] = state.deltaOffsets.back().tick;
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
    // Clear snapshot-level dirty state and hasNewDelta flags
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->state.hasNewDelta = false;
        chunkPtr->world.clearSnapshotDelta();
        for (auto ent : chunkPtr->entities) {
            registry.get<DirtyComponent>(ent).clearSnapshot();
        }
    }
}

void GameEngine::serializeTickDelta() {
    // Build each chunk's tick delta once (future: parallelisable over chunks)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->buildTickDelta(registry, tick);
    }
    // Dispatch one batch per gateway (only chunks that produced a new delta)
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            Chunk* chunk = chunkRegistry.getChunkMutable(cid);
            if (!chunk) continue;
            const auto& state = chunk->state;
            if (!state.hasNewDelta) continue;
            const size_t off = state.deltaOffsets.back().offset;
            NetworkProtocol::appendFramed(batchBuf, state.deltas.data() + off, state.deltas.size() - off);
            gwInfo.lastStateTick[cid] = state.deltaOffsets.back().tick;
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
    // Clear tick-level dirty state and hasNewDelta flags
    for (auto& [cid, chunkPtr] : chunkRegistry.getAllChunksMutable()) {
        chunkPtr->state.hasNewDelta = false;
        chunkPtr->world.clearTickDelta();
        for (auto ent : chunkPtr->entities) {
            registry.get<DirtyComponent>(ent).clearTick();
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

void GameEngine::tick() {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    ++tickCount;
    const uint32_t tick = static_cast<uint32_t>(tickCount);

    // Destroy any pending deletions from previous tick first
    ChunkMembershipSystem::destroyPendingDeletions(registry, pendingDeletions);

    InputSystem::apply(registry);
    SheepAISystem::apply(registry, tick);
    stepPhysics();

    // Phase A: Mark chunk changes for moved entities
    ChunkMembershipSystem::detectChunkCrossings(registry, chunkRegistry, tickCount, ACTIVATION_RADIUS);

    // Phase B: Process all entity lifecycle events (CREATE, DELETE, CHUNK_CHANGE)
    // Deleted entities are NOT destroyed yet - they're stored in pendingDeletions
    auto entityResult = ChunkMembershipSystem::updateEntitiesChunks(registry, chunkRegistry, tickCount, worldGenerator);
    pendingDeletions = std::move(entityResult.entitiesToDestroy);

    // Phase C: Rebuild gateway watchedChunks and dispatch snapshots
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf = ChunkMembershipSystem::rebuildGatewayWatchedChunks(
            gwInfo, chunkRegistry, playerEntities, registry, tick, WATCH_RADIUS, ACTIVATION_RADIUS, worldGenerator);

        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }

    if (tickCount % SNAPSHOT_DELTA_INTERVAL == 0) {
        serializeSnapshotDelta();
    } else {
        serializeTickDelta();
    }
    
    // Entities in pendingDeletions will be destroyed at the start of next tick
    // This ensures their DELETE deltas have been sent
}

} // namespace voxelmmo
