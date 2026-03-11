#include "game/entities/EntityFactory.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/SheepEntity.hpp"

namespace voxelmmo {

void EntityFactory::registerSpawnImpl(EntityType type, SpawnImplFn fn) {
    spawnImpls_[type] = std::move(fn);
}

void EntityFactory::spawn(EntityType type, SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z) {
    pending_.push_back(EntitySpawnRequest::make(type, x, y, z));
}

void EntityFactory::spawnPlayer(EntityType type, SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z, PlayerId playerId) {
    pending_.push_back(EntitySpawnRequest::makePlayer(type, x, y, z, playerId));
}

void EntityFactory::spawnAI(EntityType type, SubVoxelCoord x, SubVoxelCoord y, SubVoxelCoord z, uint32_t startTick) {
    pending_.push_back(EntitySpawnRequest::makeAI(type, x, y, z, startTick));
}

void EntityFactory::spawnRequest(EntitySpawnRequest req) {
    pending_.push_back(std::move(req));
}

void EntityFactory::createEntities(
    entt::registry& registry,
    ChunkRegistry& chunkRegistry,
    const std::function<GlobalEntityId()>& acquireId)
{
    for (const auto& req : pending_) {

        auto it = spawnImpls_.find(req.type);
        if (it == spawnImpls_.end()) {
            // Unknown type - skip (could also assert or throw)
            continue;
        }

        GlobalEntityId globalId = acquireId();
        entt::entity ent = it->second(registry, globalId, req);

        // Add non-player entity to its chunk
        const ChunkId chunkId = ChunkId::fromSubVoxelPos(req.x, req.y, req.z);
        chunkRegistry.addEntity(chunkId, ent);
    }

    pending_.clear();
}

std::unique_ptr<EntityFactory> createDefaultEntityFactory() {
    auto factory = std::make_unique<EntityFactory>();
    
    factory->registerSpawnImpl(EntityType::GHOST_PLAYER, GhostPlayerEntity::spawnImpl);
    factory->registerSpawnImpl(EntityType::PLAYER, PlayerEntity::spawnImpl);
    factory->registerSpawnImpl(EntityType::SHEEP, SheepEntity::spawnImpl);
    
    return factory;
}

} // namespace voxelmmo
