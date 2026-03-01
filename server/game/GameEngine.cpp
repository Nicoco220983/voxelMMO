#include "game/GameEngine.hpp"
#include <cmath>
#include <cstring>

namespace voxelmmo {

GameEngine::GameEngine() = default;

void GameEngine::setOutputCallback(OutputCallback cb) {
    outputCallback = std::move(cb);
}

// TODO: remove it when migrated to sockets using writev
void GameEngine::appendToBatch(const uint8_t* data, size_t size) {
    if (size == 0) return;
    const uint32_t len = static_cast<uint32_t>(size);
    const auto* lenBytes = reinterpret_cast<const uint8_t*>(&len);
    batchBuf.insert(batchBuf.end(), lenBytes, lenBytes + 4);
    batchBuf.insert(batchBuf.end(), data, data + size);
}

void GameEngine::appendToBatch(const std::vector<uint8_t>& msg) {
    appendToBatch(msg.data(), msg.size());
}

// ── Player input ──────────────────────────────────────────────────────────

void GameEngine::handlePlayerInput(PlayerId playerId, const uint8_t* data, size_t size) {
    if (size < 12) return;

    float vx, vy, vz;
    std::memcpy(&vx, data + 0, sizeof(float));
    std::memcpy(&vy, data + 4, sizeof(float));
    std::memcpy(&vz, data + 8, sizeof(float));

    auto it = playerEntities.find(playerId);
    if (it == playerEntities.end()) return;

    const auto& dyn = registry.get<DynamicPositionComponent>(it->second);
    // Position is always current; just apply the new velocity.
    // grounded=true: ghost player is never subject to gravity.
    DynamicPositionComponent::modify(registry, it->second,
        dyn.x, dyn.y, dyn.z,
        vx, vy, vz,
        true,   // grounded
        true);  // dirty — velocity changed, send delta to clients
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
    registry.emplace<DynamicPositionComponent>(ent, sx, sy, sz, 0.0f, 0.0f, 0.0f, false);
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
    auto& gwInfo = it->second;
    const uint32_t tick = static_cast<uint32_t>(tickCount);
    batchBuf.clear();
    for (const ChunkId& cid : gwInfo.watchedChunks) {
        auto cit = chunks.find(cid);
        if (cit == chunks.end()) continue;
        appendToBatch(cit->second->buildSnapshot(registry, entityMap, tick));
        gwInfo.lastStateTick[cid] = tick;
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
    // Dispatch one batch per gateway (only chunks that produced a new delta)
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            auto it = chunks.find(cid);
            if (it == chunks.end()) continue;
            const auto& state = it->second->state;
            if (!state.hasNewDelta) continue;
            const size_t off = state.deltaOffsets.back().offset;
            appendToBatch(state.deltas.data() + off, state.deltas.size() - off);
            gwInfo.lastStateTick[cid] = state.deltaOffsets.back().tick;
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
    // Clear snapshot-level dirty state and hasNewDelta flags
    for (auto& [cid, chunk] : chunks) {
        chunk->state.hasNewDelta = false;
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
    // Dispatch one batch per gateway (only chunks that produced a new delta)
    for (auto& [gwId, gwInfo] : gateways) {
        batchBuf.clear();
        for (const ChunkId& cid : gwInfo.watchedChunks) {
            auto it = chunks.find(cid);
            if (it == chunks.end()) continue;
            const auto& state = it->second->state;
            if (!state.hasNewDelta) continue;
            const size_t off = state.deltaOffsets.back().offset;
            appendToBatch(state.deltas.data() + off, state.deltas.size() - off);
            gwInfo.lastStateTick[cid] = state.deltaOffsets.back().tick;
        }
        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
    // Clear tick-level dirty state and hasNewDelta flags
    for (auto& [cid, chunk] : chunks) {
        chunk->state.hasNewDelta = false;
        chunk->world.clearTickDelta();
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
        if (dyn.grounded) {
            // No gravity. Advance position by velocity (covers ghost player and
            // future ground entities). Skip if stationary.
            if (dyn.vx == 0.0f && dyn.vy == 0.0f && dyn.vz == 0.0f) return;
            DynamicPositionComponent::modify(registry, ent,
                dyn.x + dyn.vx * TICK_DT,
                dyn.y + dyn.vy * TICK_DT,
                dyn.z + dyn.vz * TICK_DT,
                dyn.vx, dyn.vy, dyn.vz,
                true,   // still grounded
                false); // not dirty — routine advance, client already knows velocity
        } else {
            // Apply gravity for one tick.
            const float ny  = dyn.y  + dyn.vy * TICK_DT - 0.5f * GRAVITY * TICK_DT * TICK_DT;
            const float nvy = dyn.vy - GRAVITY * TICK_DT;
            if (ny <= FLOOR_Y) {
                // Landed: snap to floor, zero vertical velocity, mark dirty.
                DynamicPositionComponent::modify(registry, ent,
                    dyn.x + dyn.vx * TICK_DT, FLOOR_Y, dyn.z + dyn.vz * TICK_DT,
                    dyn.vx, 0.0f, dyn.vz,
                    true,   // now grounded
                    true);  // dirty — grounded state and vy changed
            } else {
                // Still airborne: advance silently, client applies the same equations.
                DynamicPositionComponent::modify(registry, ent,
                    dyn.x + dyn.vx * TICK_DT, ny, dyn.z + dyn.vz * TICK_DT,
                    dyn.vx, nvy, dyn.vz,
                    false,  // still airborne
                    false); // not dirty — routine advance
            }
        }
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
