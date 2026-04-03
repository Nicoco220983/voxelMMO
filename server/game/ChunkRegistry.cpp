#include "game/ChunkRegistry.hpp"
#include "game/WorldGenerator.hpp"
#include "game/SaveSystem.hpp"
#include "game/entities/EntityFactory.hpp"
#include <iostream>

namespace voxelmmo {

Chunk* ChunkRegistry::generate(WorldGenerator& generator, ChunkId id, SaveSystem* saveSystem) {
    auto it = chunks_.find(id);
    if (it != chunks_.end()) {
        return it->second.get();  // Already exists
    }

    auto chunk = std::make_unique<Chunk>(id);
    
    // Try to load from save first
    bool loadedFromSave = false;
    if (saveSystem && saveSystem->hasSavedChunk(id)) {
        loadedFromSave = saveSystem->loadChunkVoxels(id, chunk->world.voxels);
        if (loadedFromSave) {
            std::cout << "[ChunkRegistry] Loaded saved chunk (" 
                      << id.x() << "," << id.y() << "," << id.z() << ")\n";
        }
    }
    
    // Generate procedurally if not loaded from save
    if (!loadedFromSave) {
        generator.generate(chunk->world.voxels, id.x(), id.y(), id.z());
    }
    
    // Build the cached physics type array (parallel to voxels)
    chunk->world.rebuildPhysicTypeCache();
    
    Chunk* ptr = chunk.get();
    chunks_[id] = std::move(chunk);
    
    // Save new chunks immediately to ensure persistence
    if (!loadedFromSave && saveSystem) {
        saveSystem->saveChunkVoxels(id, ptr->world.voxels);
    }
    
    return ptr;
}

Chunk* ChunkRegistry::createOrGet(ChunkId id) {
    auto it = chunks_.find(id);
    if (it != chunks_.end()) {
        return it->second.get();
    }

    auto chunk = std::make_unique<Chunk>(id);
    Chunk* ptr = chunk.get();
    chunks_[id] = std::move(chunk);
    return ptr;
}

Chunk* ChunkRegistry::activate(ChunkId id, WorldGenerator& generator, EntityFactory& entityFactory, uint32_t tick) {
    auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        return nullptr;  // Chunk doesn't exist - caller must generate first
    }

    Chunk* chunk = it->second.get();
    if (chunk->activated) {
        return chunk;  // Already activated
    }

    chunk->activated = true;
    
    // Generate entities for the newly activated chunk
    generator.generateEntities(id, entityFactory, tick, *this);
    
    return chunk;
}

bool ChunkRegistry::deactivate(ChunkId id, entt::registry& registry) {
    auto it = chunks_.find(id);
    if (it == chunks_.end() || !it->second->activated) {
        return false;
    }

    Chunk* chunk = it->second.get();
    chunk->activated = false;

    // Remove all non-player entities from this chunk
    std::vector<entt::entity> toRemove;
    toRemove.reserve(chunk->entities.size());
    
    for (entt::entity ent : chunk->entities) {
        // Keep players (they persist across chunk deactivation)
        if (registry.valid(ent) && !registry.all_of<PlayerComponent>(ent)) {
            toRemove.push_back(ent);
        }
    }

    for (entt::entity ent : toRemove) {
        chunk->entities.erase(ent);
        if (registry.valid(ent)) {
            registry.destroy(ent);
        }
    }

    return true;
}

bool ChunkRegistry::unload(ChunkId id) {
    auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        return false;
    }
    
    // Safety check: don't unload if still activated
    if (it->second->activated) {
        std::cerr << "[ChunkRegistry] Warning: Cannot unload active chunk ("
                  << id.x() << "," << id.y() << "," << id.z() << ")\n";
        return false;
    }
    
    chunks_.erase(it);
    return true;
}

bool ChunkRegistry::isActive(ChunkId id) const {
    auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        return false;
    }
    return it->second->activated;
}

bool ChunkRegistry::addEntity(ChunkId chunkId, entt::entity entity) {
    if (Chunk* chunk = getChunkMutable(chunkId)) {
        chunk->entities.insert(entity);
        return true;
    }
    std::cerr << "[ChunkRegistry] Warning: addEntity failed - chunk (" 
              << chunkId.x() << "," << chunkId.y() << "," << chunkId.z() 
              << ") does not exist\n";
    return false;
}

bool ChunkRegistry::addPlayerEntity(ChunkId chunkId, entt::entity entity, PlayerId playerId) {
    if (Chunk* chunk = getChunkMutable(chunkId)) {
        chunk->entities.insert(entity);
        chunk->presentPlayers.insert(playerId);
        return true;
    }
    std::cerr << "[ChunkRegistry] Warning: addPlayerEntity failed - chunk (" 
              << chunkId.x() << "," << chunkId.y() << "," << chunkId.z() 
              << ") does not exist\n";
    return false;
}

} // namespace voxelmmo
