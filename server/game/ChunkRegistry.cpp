#include "game/ChunkRegistry.hpp"
#include "game/WorldGenerator.hpp"

namespace voxelmmo {

Chunk* ChunkRegistry::generate(WorldGenerator& generator, ChunkId id) {
    auto it = chunks_.find(id);
    if (it != chunks_.end()) {
        return it->second.get();  // Already exists
    }

    auto chunk = std::make_unique<Chunk>(id);
    generator.generate(chunk->world.voxels, id.x(), id.y(), id.z());
    
    Chunk* ptr = chunk.get();
    chunks_[id] = std::move(chunk);
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

Chunk* ChunkRegistry::activate(ChunkId id) {
    auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        return nullptr;  // Chunk doesn't exist - caller must generate first
    }

    Chunk* chunk = it->second.get();
    if (chunk->activated) {
        return chunk;  // Already activated
    }

    chunk->activated = true;
    return chunk;
}

Chunk* ChunkRegistry::generateAndActivate(WorldGenerator& generator, ChunkId id) {
    Chunk* chunk = generate(generator, id);
    if (chunk) {
        chunk->activated = true;
    }
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

} // namespace voxelmmo
