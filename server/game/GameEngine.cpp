#include "game/GameEngine.hpp"
#include <cmath>

namespace voxelmmo {

GameEngine::GameEngine() = default;

void GameEngine::setOutputCallback(OutputCallback cb) {
    outputCallback = std::move(cb);
}

void GameEngine::appendToBatch(const std::vector<uint8_t>& msg) {
    if (msg.empty()) return;
    const uint32_t len = static_cast<uint32_t>(msg.size());
    const auto* lenBytes = reinterpret_cast<const uint8_t*>(&len);
    batchBuf.insert(batchBuf.end(), lenBytes, lenBytes + 4);
    batchBuf.insert(batchBuf.end(), msg.begin(), msg.end());
}

// ── Gateway management ────────────────────────────────────────────────────

void GameEngine::registerGateway(GatewayId gwId) {
    gateways.emplace(gwId, GatewayInfo{});
}

void GameEngine::unregisterGateway(GatewayId gwId) {
    auto it = gateways.find(gwId);
    if (it == gateways.end()) return;
    for (PlayerId pid : it->second.players) removePlayer(pid);
    gateways.erase(it);
}

// ── Player management ─────────────────────────────────────────────────────

EntityId GameEngine::addPlayer(GatewayId gwId, PlayerId playerId,
                                float sx, float sy, float sz)
{
    const entt::entity ent = registry.create();
    registry.emplace<DynamicPositionComponent>(ent, tickCount, sx, sy, sz, 0.0f, 0.0f, 0.0f, false);
    registry.emplace<DirtyComponent>(ent);

    const EntityId eid = nextEntityId++;
    auto player = std::make_unique<PlayerEntity>(registry, ent, eid, playerId);
    entityMap[eid]         = std::move(player);
    playerEntities[playerId] = ent;

    if (auto it = gateways.find(gwId); it != gateways.end()) {
        it->second.players.insert(playerId);
    }
    return eid;
}

void GameEngine::removePlayer(PlayerId playerId) {
    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return;
    registry.destroy(it->second);
    playerEntities.erase(it);
    // Remove from any chunk's present/watching sets
    for (auto& [cid, chunk] : chunks) {
        chunk->presentPlayers.erase(playerId);
        chunk->watchingPlayers.erase(playerId);
    }
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
    // watchedChunks is populated by checkPlayersChunks(); run it now so a
    // snapshot requested immediately after addPlayer() is not empty.
    checkPlayersChunks();
    serializeSnapshot(gwId);
}

void GameEngine::serializeSnapshot(GatewayId gwId) {
    auto it = gateways.find(gwId);
    if (it == gateways.end()) return;
    batchBuf.clear();
    for (const ChunkId& cid : it->second.watchedChunks) {
        auto cit = chunks.find(cid);
        if (cit == chunks.end()) continue;
        appendToBatch(cit->second->buildSnapshot(registry, entityMap, static_cast<uint32_t>(tickCount)));
    }
    if (!batchBuf.empty() && outputCallback)
        outputCallback(gwId, batchBuf.data(), batchBuf.size());
}

void GameEngine::serializeSnapshotDelta() {
    // Build each chunk's delta once (future: parallelisable over chunks)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    for (auto& [cid, chunk] : chunks) {
        chunk->buildSnapshotDelta(registry, entityMap, tick);
    }
    // Dispatch one batch per gateway
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            auto it = chunks.find(cid);
            if (it == chunks.end()) continue;
            appendToBatch(it->second->state.snapshotDelta);
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
    // Clear snapshot-level dirty state
    for (auto& [cid, chunk] : chunks) {
        chunk->world.clearSnapshotDelta();
        for (EntityId eid : chunk->entities) {
            auto eit = entityMap.find(eid);
            if (eit != entityMap.end()) {
                registry.get<DirtyComponent>(eit->second->handle).clearSnapshot();
            }
        }
    }
}

void GameEngine::serializeTickDelta() {
    // Build each chunk's tick delta once (future: parallelisable over chunks)
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    for (auto& [cid, chunk] : chunks) {
        chunk->buildTickDelta(registry, entityMap, tick);
    }
    // Dispatch one batch per gateway
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            auto it = chunks.find(cid);
            if (it == chunks.end()) continue;
            if (it->second->state.tickDeltas.empty()) continue;
            appendToBatch(it->second->state.tickDeltas.back());
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
    // Clear tick-level dirty state and accumulated tick deltas
    for (auto& [cid, chunk] : chunks) {
        chunk->world.clearTickDelta();
        chunk->state.tickDeltas.clear();
        for (EntityId eid : chunk->entities) {
            auto eit = entityMap.find(eid);
            if (eit != entityMap.end()) {
                registry.get<DirtyComponent>(eit->second->handle).clearTick();
            }
        }
    }
}

// ── Physics ───────────────────────────────────────────────────────────────

void GameEngine::stepPhysics() {
    // Simple planar ground — TODO: replace with voxel collision
    static constexpr float FLOOR_Y = static_cast<float>(CHUNK_SIZE_Y); // 16.0f

    auto view = registry.view<DynamicPositionComponent, DirtyComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn, DirtyComponent&) {
        if (dyn.grounded) return;  // client predicts correctly; nothing to do

        auto [px, py, pz] = DynamicPositionComponent::predictAt(dyn, tickCount);
        if (py <= FLOOR_Y) {
            // Entity has landed — snap to floor and stop vertical movement
            DynamicPositionComponent::modify(registry, ent, tickCount,
                px, FLOOR_Y, pz, dyn.vx, 0.0f, dyn.vz, true);
        }
        // else: still airborne — both sides predict identically, no update needed
    });
}

// ── Chunk membership ──────────────────────────────────────────────────────

void GameEngine::checkPlayersChunks() {
    // Clear stale watching sets
    for (auto& [cid, chunk] : chunks) {
        chunk->presentPlayers.clear();
        chunk->watchingPlayers.clear();
    }

    for (auto& [gwId, gwInfo] : gateways) {
        gwInfo.watchedChunks.clear();

        for (PlayerId pid : gwInfo.players) {
            auto entIt = playerEntities.find(pid);
            if (entIt == playerEntities.end()) continue;

            const auto& dyn = registry.get<DynamicPositionComponent>(entIt->second);
            const int32_t cx = static_cast<int32_t>(std::floor(dyn.x / CHUNK_SIZE_X));
            const int8_t  cy = static_cast<int8_t> (std::floor(dyn.y / CHUNK_SIZE_Y));
            const int32_t cz = static_cast<int32_t>(std::floor(dyn.z / CHUNK_SIZE_Z));

            for (int32_t dx = -WATCH_RADIUS; dx <= WATCH_RADIUS; ++dx) {
                for (int8_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dz = -WATCH_RADIUS; dz <= WATCH_RADIUS; ++dz) {
                        const ChunkId cid = ChunkId::make(
                            static_cast<int8_t>(cy + dy), cx + dx, cz + dz);
                        gwInfo.watchedChunks.insert(cid);

                        // Activate chunks within the tighter activation radius
                        if (std::abs(dx) <= ACTIVATION_RADIUS &&
                            std::abs(dz) <= ACTIVATION_RADIUS)
                        {
                            Chunk& chunk = activateChunk(cid);
                            chunk.watchingPlayers.insert(pid);
                        }
                    }
                }
            }

            // Mark player as present in their exact chunk
            const ChunkId playerChunk = ChunkId::make(cy, cx, cz);
            if (auto cit = chunks.find(playerChunk); cit != chunks.end()) {
                cit->second->presentPlayers.insert(pid);
            }
        }
    }
}

// ── Main tick ─────────────────────────────────────────────────────────────

void GameEngine::tick() {
    ++tickCount;

    stepPhysics();
    checkPlayersChunks();

    if (tickCount % SNAPSHOT_DELTA_INTERVAL == 0) {
        serializeSnapshotDelta();
    } else {
        serializeTickDelta();
    }
}

} // namespace voxelmmo
