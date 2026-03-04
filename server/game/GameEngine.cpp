#include "game/GameEngine.hpp"
#include "game/systems/InputSystem.hpp"
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
    const entt::entity ent = registry.create();
    fit->second(registry, ent, sx, sy, sz, playerId);
    playerEntities[playerId] = ent;
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

void GameEngine::removePlayer(PlayerId playerId) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return;
    const entt::entity ent = it->second;

    // Clean up chunk membership before destroying
    if (registry.all_of<ChunkMemberComponent>(ent)) {
        const auto& cm = registry.get<ChunkMemberComponent>(ent);
        if (cm.chunkAssigned) {
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
    }

    registry.destroy(ent);
    playerEntities.erase(it);
}

// ── Chunk activation ──────────────────────────────────────────────────────

Chunk& GameEngine::activateChunk(ChunkId id) {
    auto it = chunks.find(id);
    if (it != chunks.end()) return *it->second;

    auto chunk = std::make_unique<Chunk>(id);
    chunk->world.generate(id.x(), id.y(), id.z());
    Chunk& ref = *chunk;
    chunks[id] = std::move(chunk);
    return ref;
}

// ── Serialisation helpers ─────────────────────────────────────────────────

void GameEngine::sendSnapshot(GatewayId gwId) {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    // watchedChunks is populated by checkPlayersChunks(); run it now so a
    // snapshot requested immediately after addPlayer() is not empty.
    checkEntitiesChunks();
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
        for (auto& [ent, ceid] : chunk->entities) {
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
        for (auto& [ent, ceid] : chunk->entities) {
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

// ── Chunk membership ──────────────────────────────────────────────────────

void GameEngine::checkEntitiesChunks() {
    const uint32_t tick = static_cast<uint32_t>(tickCount);

    // ── Phase A: incrementally update per-chunk lists for moved entities ──
    //
    // Per-chunk sets (entities, presentPlayers, watchingPlayers) are never
    // cleared wholesale.  We only touch them when an entity crosses a chunk
    // boundary, identified by the DynamicPositionComponent::moved flag.

    struct SelfMsg { GatewayId gwId; ChunkId chunkId; uint16_t entityId; };
    std::vector<SelfMsg> selfMsgs;

    auto view = registry.view<DynamicPositionComponent, ChunkMemberComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn, ChunkMemberComponent& cm) {
        if (!dyn.moved) return;
        dyn.moved = false;

        const int32_t cx = dyn.x >> CHUNK_SHIFT_X;
        const int32_t cy = dyn.y >> CHUNK_SHIFT_Y;
        const int32_t cz = dyn.z >> CHUNK_SHIFT_Z;
        const ChunkId newChunk = ChunkId::make(cy, cx, cz);

        if (cm.chunkAssigned && newChunk == cm.currentChunkId) return;

        const bool     isPlayer = registry.all_of<PlayerComponent>(ent);
        const PlayerId pid      = isPlayer ? registry.get<PlayerComponent>(ent).playerId : 0u;

        // Remove from old chunk lists
        if (cm.chunkAssigned) {
            const int32_t ocx = cm.currentChunkId.x();
            const int32_t ocy = cm.currentChunkId.y();
            const int32_t ocz = cm.currentChunkId.z();

            if (auto it = chunks.find(cm.currentChunkId); it != chunks.end()) {
                it->second->entities.erase(ent);
                if (isPlayer) it->second->presentPlayers.erase(pid);
            }
            if (isPlayer) {
                for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
                for (int32_t dy = -1; dy <= 1; ++dy)
                for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
                    const ChunkId cid = ChunkId::make(ocy + dy, ocx + dx, ocz + dz);
                    if (auto it = chunks.find(cid); it != chunks.end())
                        it->second->watchingPlayers.erase(pid);
                }
            }
        }

        // Add to new chunk lists
        Chunk& nc = activateChunk(newChunk);
        nc.entities[ent] = nc.nextChunkEntityId_++;
        if (isPlayer) {
            nc.presentPlayers.insert(pid);
            for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
            for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
                const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
                activateChunk(cid).watchingPlayers.insert(pid);
            }
            // Tell the gateway which entity is "self" for this player
            const uint16_t ceid = static_cast<uint16_t>(nc.nextChunkEntityId_ - 1);
            for (auto& [gwId2, gwInfo2] : gateways) {
                if (gwInfo2.players.count(pid)) {
                    selfMsgs.push_back({gwId2, newChunk, ceid});
                    break;
                }
            }
        }

        cm.currentChunkId = newChunk;
        cm.chunkAssigned  = true;
    });

    // ── Phase B: rebuild gateway watchedChunks + dispatch new snapshots ───
    //
    // Per-gateway watchedChunks is rebuilt from scratch each tick (cheap: set
    // insertions over WATCH_RADIUS³ chunks per player).  Per-chunk membership
    // lists are NOT touched here — Phase A owns those.

    for (auto& [gwId, gwInfo] : gateways) {
        gwInfo.watchedChunks.clear();
        batchBuf.clear();

        for (PlayerId pid : gwInfo.players) {
            auto entIt = playerEntities.find(pid);
            if (entIt == playerEntities.end()) continue;

            const auto& dyn = registry.get<DynamicPositionComponent>(entIt->second);
            const int32_t cx = dyn.x >> CHUNK_SHIFT_X;
            const int32_t cy = dyn.y >> CHUNK_SHIFT_Y;
            const int32_t cz = dyn.z >> CHUNK_SHIFT_Z;

            for (int32_t dx = -WATCH_RADIUS; dx <= WATCH_RADIUS; ++dx) {
                for (int32_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dz = -WATCH_RADIUS; dz <= WATCH_RADIUS; ++dz) {
                        const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
                        gwInfo.watchedChunks.insert(cid);

                        // Ensure activation-radius chunks exist; send snapshot on first sight
                        if (std::abs(dx) <= ACTIVATION_RADIUS &&
                            std::abs(dz) <= ACTIVATION_RADIUS)
                        {
                            Chunk& chunk = activateChunk(cid);
                            if (!gwInfo.lastStateTick.count(cid)) {
                                NetworkProtocol::appendFramed(batchBuf, chunk.buildSnapshot(registry, tick));
                                gwInfo.lastStateTick[cid] = tick;
                            }
                        }
                    }
                }
            }
        }

        // Append SELF_ENTITY messages for any player in this gateway who entered a new chunk
        for (const auto& sm : selfMsgs) {
            if (sm.gwId != gwId) continue;
            const auto msg = NetworkProtocol::buildSelfEntityMessage(sm.chunkId, tick, sm.entityId);
            NetworkProtocol::appendFramed(batchBuf, msg.data(), msg.size());
        }

        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
}

// ── Main tick ─────────────────────────────────────────────────────────────

void GameEngine::tick() {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    ++tickCount;

    InputSystem::apply(registry);
    stepPhysics();
    checkEntitiesChunks();

    if (tickCount % SNAPSHOT_DELTA_INTERVAL == 0) {
        serializeSnapshotDelta();
    } else {
        serializeTickDelta();
    }
}

} // namespace voxelmmo
