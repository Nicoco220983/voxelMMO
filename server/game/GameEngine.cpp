#include "game/GameEngine.hpp"
#include "game/systems/InputSystem.hpp"
#include "game/components/SheepBehaviorComponent.hpp"
#include "common/MessageTypes.hpp"
#include <cmath>

namespace voxelmmo {

GameEngine::GameEngine() = default;

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

    fit->second(registry, ent, sx, sy, sz, playerId, chunkId);
    playerEntities[playerId] = ent;

    // Add to chunk immediately (track by entt handle, not wire ID)
    Chunk& chunk = getOrActivateChunk(chunkId);
    chunk.entities.insert(ent);
    chunk.presentPlayers.insert(playerId);

    // Add to watching radius
    for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
    for (int32_t dy = -1; dy <= 1; ++dy)
    for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
        const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
        getOrActivateChunk(cid).watchingPlayers.insert(playerId);
    }

    if (auto it = gateways.find(gwId); it != gateways.end())
        it->second.players.insert(playerId);

    // Send SELF_ENTITY message once at creation (global ID is stable across chunk moves)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
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

    // Clean up chunk membership before destroying
    if (registry.all_of<ChunkMembershipComponent>(ent)) {
        const auto& cm = registry.get<ChunkMembershipComponent>(ent);
        const int32_t ocx = cm.currentChunkId.x();
        const int32_t ocy = cm.currentChunkId.y();
        const int32_t ocz = cm.currentChunkId.z();
        if (auto cit = chunks.find(cm.currentChunkId); cit != chunks.end()) {
            cit->second->entities.erase(ent);
            cit->second->presentPlayers.erase(playerId);
        }
        for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
        for (int32_t dy = -1; dy <= 1; ++dy)
        for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
            const ChunkId cid = ChunkId::make(ocy + dy, ocx + dx, ocz + dz);
            if (auto cit = chunks.find(cid); cit != chunks.end())
                cit->second->watchingPlayers.erase(playerId);
        }
    }

    registry.destroy(ent);
    playerEntities.erase(it);
}

// ── Chunk activation ──────────────────────────────────────────────────────

Chunk& GameEngine::getOrActivateChunk(ChunkId id) {
    auto it = chunks.find(id);
    if (it != chunks.end()) return *it->second;

    auto chunk = std::make_unique<Chunk>(id);
    chunk->world.generate(id.x(), id.y(), id.z());
    
    // Generate entities for this chunk (sheep, etc.)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    chunk->generator.generateEntities(id, registry, tick);
    
    // Add any newly created sheep entities to this chunk's entity set
    registry.view<SheepBehaviorComponent, ChunkMembershipComponent, GlobalEntityIdComponent>()
        .each([&](entt::entity ent, const SheepBehaviorComponent&, const ChunkMembershipComponent& cm, const GlobalEntityIdComponent&) {
            if (cm.currentChunkId == id) {
                chunk->entities.insert(ent);
            }
        });
    
    Chunk& ref = *chunk;
    chunks[id] = std::move(chunk);
    return ref;
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
        it->second, chunks, playerEntities, registry, tick, WATCH_RADIUS, ACTIVATION_RADIUS);
    ChunkMembershipSystem::updateEntities(registry, chunks, tickCount, ACTIVATION_RADIUS);
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
        auto cit = chunks.find(cid);
        if (cit == chunks.end()) continue;
        NetworkProtocol::appendFramed(batchBuf, cit->second->buildSnapshot(registry, tick));
        gwInfo.lastStateTick[cid] = tick;
    }
    if (!batchBuf.empty() && outputCallback)
        outputCallback(gwId, batchBuf.data(), batchBuf.size());
}

void GameEngine::serializeSnapshotDelta() {
    // Build each chunk's delta once (future: parallelisable over chunks)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    for (auto& [cid, chunk] : chunks) {
        chunk->buildSnapshotDelta(registry, tick);
    }
    // Dispatch one batch per gateway (only chunks that produced a new delta)
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            auto it = chunks.find(cid);
            if (it == chunks.end()) continue;
            const auto& state = it->second->state;
            if (!state.hasNewDelta) continue;
            const size_t off = state.deltaOffsets.back().offset;
            NetworkProtocol::appendFramed(batchBuf, state.deltas.data() + off, state.deltas.size() - off);
            gwInfo.lastStateTick[cid] = state.deltaOffsets.back().tick;
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
    // Clear snapshot-level dirty state and hasNewDelta flags
    for (auto& [cid, chunk] : chunks) {
        chunk->state.hasNewDelta = false;
        chunk->world.clearSnapshotDelta();
        for (auto ent : chunk->entities) {
            registry.get<DirtyComponent>(ent).clearSnapshot();
        }
    }
}

void GameEngine::serializeTickDelta() {
    // Build each chunk's tick delta once (future: parallelisable over chunks)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    for (auto& [cid, chunk] : chunks) {
        chunk->buildTickDelta(registry, tick);
    }
    // Dispatch one batch per gateway (only chunks that produced a new delta)
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            auto it = chunks.find(cid);
            if (it == chunks.end()) continue;
            const auto& state = it->second->state;
            if (!state.hasNewDelta) continue;
            const size_t off = state.deltaOffsets.back().offset;
            NetworkProtocol::appendFramed(batchBuf, state.deltas.data() + off, state.deltas.size() - off);
            gwInfo.lastStateTick[cid] = state.deltaOffsets.back().tick;
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
    // Clear tick-level dirty state and hasNewDelta flags
    for (auto& [cid, chunk] : chunks) {
        chunk->state.hasNewDelta = false;
        chunk->world.clearTickDelta();
        for (auto ent : chunk->entities) {
            registry.get<DirtyComponent>(ent).clearTick();
        }
    }
}

// ── Physics ───────────────────────────────────────────────────────────────

void GameEngine::stepPhysics() {
    PhysicsSystem::apply(registry, chunks);
}

// ── Chunk lookup ──────────────────────────────────────────────────────────

Chunk* GameEngine::chunkAt(int32_t px, int32_t py, int32_t pz) noexcept {
    const auto it = chunks.find(chunkIdOf(px, py, pz));
    return it != chunks.end() ? it->second.get() : nullptr;
}



// ── Main tick ─────────────────────────────────────────────────────────────

void GameEngine::tick() {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    ++tickCount;
    const uint32_t tick = static_cast<uint32_t>(tickCount);

    InputSystem::apply(registry);
    SheepAISystem::apply(registry, tick);
    stepPhysics();

    // Phase A: Update chunk membership for moved entities
    ChunkMembershipSystem::updateEntities(registry, chunks, tickCount, ACTIVATION_RADIUS);

    // Phase B: Rebuild gateway watchedChunks and dispatch snapshots
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf = ChunkMembershipSystem::rebuildGatewayWatchedChunks(
            gwInfo, chunks, playerEntities, registry, tick, WATCH_RADIUS, ACTIVATION_RADIUS);

        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }

    if (tickCount % SNAPSHOT_DELTA_INTERVAL == 0) {
        serializeSnapshotDelta();
    } else {
        serializeTickDelta();
    }
}

} // namespace voxelmmo
