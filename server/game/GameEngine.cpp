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
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    if (size < 12) return;

    // Receive 3 × float32 (voxels/s) from client — unchanged wire format.
    float fvx, fvy, fvz;
    std::memcpy(&fvx, data + 0, sizeof(float));
    std::memcpy(&fvy, data + 4, sizeof(float));
    std::memcpy(&fvz, data + 8, sizeof(float));

    // Convert voxels/s → sub-voxels/tick.
    const int32_t vx = static_cast<int32_t>(std::round(fvx * SUBVOXEL_SIZE * TICK_DT));
    const int32_t vy = static_cast<int32_t>(std::round(fvy * SUBVOXEL_SIZE * TICK_DT));
    const int32_t vz = static_cast<int32_t>(std::round(fvz * SUBVOXEL_SIZE * TICK_DT));

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

void GameEngine::addPlayer(GatewayId gwId, PlayerId playerId,
                            float sx, float sy, float sz)
{
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    const entt::entity ent = registry.create();
    registry.emplace<DynamicPositionComponent>(ent,
        static_cast<int32_t>(sx * SUBVOXEL_SIZE),
        static_cast<int32_t>(sy * SUBVOXEL_SIZE),
        static_cast<int32_t>(sz * SUBVOXEL_SIZE),
        0, 0, 0, false);
    registry.emplace<DirtyComponent>(ent);
    registry.emplace<EntityTypeComponent>(ent, EntityType::PLAYER);
    registry.emplace<PlayerComponent>(ent, playerId);
    registry.emplace<ChunkMemberComponent>(ent);

    playerEntities[playerId] = ent;

    if (auto it = gateways.find(gwId); it != gateways.end()) {
        it->second.players.insert(playerId);
    }
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
            const int8_t  ocy = cm.currentChunkId.y();
            const int32_t ocz = cm.currentChunkId.z();
            if (auto cit = chunks.find(cm.currentChunkId); cit != chunks.end()) {
                cit->second->entities.erase(ent);
                cit->second->presentPlayers.erase(playerId);
            }
            for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
            for (int8_t  dy = -1; dy <= 1; ++dy)
            for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
                const ChunkId cid = ChunkId::make(
                    static_cast<int8_t>(ocy + dy), ocx + dx, ocz + dz);
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
        appendToBatch(cit->second->buildSnapshot(registry, tick));
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
        for (auto& [ent, ceid] : chunk->entities) {
            registry.get<DirtyComponent>(ent).clearTick();
        }
    }
}

// ── Physics ───────────────────────────────────────────────────────────────

void GameEngine::stepPhysics() {
    auto view = registry.view<DynamicPositionComponent, DirtyComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn, DirtyComponent&) {
        if (dyn.grounded) {
            // No gravity. Advance position by velocity (covers ghost player and
            // future ground entities). Skip if stationary.
            if (dyn.vx == 0 && dyn.vy == 0 && dyn.vz == 0) return;
            DynamicPositionComponent::modify(registry, ent,
                dyn.x + dyn.vx, dyn.y + dyn.vy, dyn.z + dyn.vz,
                dyn.vx, dyn.vy, dyn.vz,
                true,   // still grounded
                false); // not dirty — routine advance, client already knows velocity
        } else {
            // Apply gravity for one tick (floor collision handled in physics plan).
            const int32_t nvy = dyn.vy - GRAVITY_DECREMENT;
            const int32_t ny  = dyn.y  + nvy;
            DynamicPositionComponent::modify(registry, ent,
                dyn.x + dyn.vx, ny, dyn.z + dyn.vz,
                dyn.vx, nvy, dyn.vz,
                false,  // still airborne
                false); // not dirty — routine advance
        }
    });
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

    auto view = registry.view<DynamicPositionComponent, ChunkMemberComponent>();
    view.each([&](entt::entity ent, DynamicPositionComponent& dyn, ChunkMemberComponent& cm) {
        if (!dyn.moved) return;
        dyn.moved = false;

        const int32_t cx = dyn.x >> CHUNK_SHIFT_X;
        const int8_t  cy = static_cast<int8_t>(dyn.y >> CHUNK_SHIFT_Y);
        const int32_t cz = dyn.z >> CHUNK_SHIFT_Z;
        const ChunkId newChunk = ChunkId::make(cy, cx, cz);

        if (cm.chunkAssigned && newChunk == cm.currentChunkId) return;

        const bool     isPlayer = registry.all_of<PlayerComponent>(ent);
        const PlayerId pid      = isPlayer ? registry.get<PlayerComponent>(ent).playerId : 0u;

        // Remove from old chunk lists
        if (cm.chunkAssigned) {
            const int32_t ocx = cm.currentChunkId.x();
            const int8_t  ocy = cm.currentChunkId.y();
            const int32_t ocz = cm.currentChunkId.z();

            if (auto it = chunks.find(cm.currentChunkId); it != chunks.end()) {
                it->second->entities.erase(ent);
                if (isPlayer) it->second->presentPlayers.erase(pid);
            }
            if (isPlayer) {
                for (int32_t dx = -ACTIVATION_RADIUS; dx <= ACTIVATION_RADIUS; ++dx)
                for (int8_t  dy = -1; dy <= 1; ++dy)
                for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
                    const ChunkId cid = ChunkId::make(
                        static_cast<int8_t>(ocy + dy), ocx + dx, ocz + dz);
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
            for (int8_t  dy = -1; dy <= 1; ++dy)
            for (int32_t dz = -ACTIVATION_RADIUS; dz <= ACTIVATION_RADIUS; ++dz) {
                const ChunkId cid = ChunkId::make(
                    static_cast<int8_t>(cy + dy), cx + dx, cz + dz);
                activateChunk(cid).watchingPlayers.insert(pid);
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
            const int8_t  cy = static_cast<int8_t>(dyn.y >> CHUNK_SHIFT_Y);
            const int32_t cz = dyn.z >> CHUNK_SHIFT_Z;

            for (int32_t dx = -WATCH_RADIUS; dx <= WATCH_RADIUS; ++dx) {
                for (int8_t dy = -1; dy <= 1; ++dy) {
                    for (int32_t dz = -WATCH_RADIUS; dz <= WATCH_RADIUS; ++dz) {
                        const ChunkId cid = ChunkId::make(
                            static_cast<int8_t>(cy + dy), cx + dx, cz + dz);
                        gwInfo.watchedChunks.insert(cid);

                        // Ensure activation-radius chunks exist; send snapshot on first sight
                        if (std::abs(dx) <= ACTIVATION_RADIUS &&
                            std::abs(dz) <= ACTIVATION_RADIUS)
                        {
                            Chunk& chunk = activateChunk(cid);
                            if (!gwInfo.lastStateTick.count(cid)) {
                                appendToBatch(chunk.buildSnapshot(registry, tick));
                                gwInfo.lastStateTick[cid] = tick;
                            }
                        }
                    }
                }
            }
        }

        if (!batchBuf.empty() && outputCallback)
            outputCallback(gwId, batchBuf.data(), batchBuf.size());
    }
}

// ── Main tick ─────────────────────────────────────────────────────────────

void GameEngine::tick() {
    std::lock_guard<std::recursive_mutex> lock(mtx_);
    ++tickCount;

    stepPhysics();
    checkEntitiesChunks();

    if (tickCount % SNAPSHOT_DELTA_INTERVAL == 0) {
        serializeSnapshotDelta();
    } else {
        serializeTickDelta();
    }
}

} // namespace voxelmmo
