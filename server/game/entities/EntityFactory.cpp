#include "game/entities/EntityFactory.hpp"
#include "game/entities/PlayerEntity.hpp"
#include "game/entities/GhostPlayerEntity.hpp"
#include "game/entities/SheepEntity.hpp"
#include "game/components/PlayerComponent.hpp"

namespace voxelmmo {

void EntityFactory::registerSpawnImpl(EntityType type, SpawnImplFn fn) {
    spawnImpls_[type] = std::move(fn);
}

void EntityFactory::spawn(EntityType type, int32_t x, int32_t y, int32_t z) {
    pending_.push_back(EntitySpawnRequest::make(type, x, y, z));
}

void EntityFactory::spawnPlayer(EntityType type, int32_t x, int32_t y, int32_t z, PlayerId playerId) {
    pending_.push_back(EntitySpawnRequest::makePlayer(type, x, y, z, playerId));
}

void EntityFactory::spawnAI(EntityType type, int32_t x, int32_t y, int32_t z, uint32_t startTick) {
    pending_.push_back(EntitySpawnRequest::makeAI(type, x, y, z, startTick));
}

void EntityFactory::spawnRequest(EntitySpawnRequest req) {
    pending_.push_back(std::move(req));
}

std::vector<entt::entity> EntityFactory::createEntities(
    entt::registry& registry,
    ChunkRegistry& chunkRegistry,
    const std::function<GlobalEntityId()>& acquireId)
{
    std::vector<entt::entity> created;
    created.reserve(pending_.size());

    for (const auto& req : pending_) {
        auto it = spawnImpls_.find(req.type);
        if (it == spawnImpls_.end()) {
            // Unknown type - skip (could also assert or throw)
            continue;
        }

        GlobalEntityId globalId = acquireId();
        entt::entity ent = it->second(registry, globalId, req);
        created.push_back(ent);

        // Add entity to its chunk based on spawn position
        const ChunkId chunkId = chunkIdOf(req.x, req.y, req.z);
        if (Chunk* chunk = chunkRegistry.getChunkMutable(chunkId)) {
            chunk->entities.insert(ent);
            
            // For players, also add to presentPlayers
            if (registry.all_of<PlayerComponent>(ent)) {
                const PlayerId pid = registry.get<PlayerComponent>(ent).playerId;
                chunk->presentPlayers.insert(pid);
            }
        }
    }

    pending_.clear();
    return created;
}

std::unique_ptr<EntityFactory> createDefaultEntityFactory() {
    auto factory = std::make_unique<EntityFactory>();
    
    factory->registerSpawnImpl(EntityType::GHOST_PLAYER, GhostPlayerEntity::spawnImpl);
    factory->registerSpawnImpl(EntityType::PLAYER, PlayerEntity::spawnImpl);
    factory->registerSpawnImpl(EntityType::SHEEP, SheepEntity::spawnImpl);
    
    return factory;
}

} // namespace voxelmmo
