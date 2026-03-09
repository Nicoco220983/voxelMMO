#pragma once
#include "common/MessageTypes.hpp"
#include "common/EntityType.hpp"
#include "common/Types.hpp"
#include "game/ChunkRegistry.hpp"
#include <entt/entt.hpp>
#include <functional>
#include <unordered_map>
#include <vector>

namespace voxelmmo {

/**
 * @brief Spawn request for deferred entity creation.
 *
 * Captures all information needed to spawn an entity at a later time.
 * Used by EntityFactory to batch entity creation at a precise engine step.
 */
struct EntitySpawnRequest {
    EntityType type;
    int32_t x, y, z;           // Position in sub-voxels
    PlayerId playerId{0};      // Only for player entities
    uint32_t startTick{0};     // Only for AI entities (e.g., sheep)

    // Constructor for non-player entities
    static EntitySpawnRequest make(EntityType type, int32_t x, int32_t y, int32_t z) {
        return EntitySpawnRequest{type, x, y, z, 0, 0};
    }

    // Constructor for player entities
    static EntitySpawnRequest makePlayer(EntityType type, int32_t x, int32_t y, int32_t z, PlayerId pid) {
        return EntitySpawnRequest{type, x, y, z, pid, 0};
    }

    // Constructor for AI entities (sheep, etc.)
    static EntitySpawnRequest makeAI(EntityType type, int32_t x, int32_t y, int32_t z, uint32_t tick) {
        return EntitySpawnRequest{type, x, y, z, 0, tick};
    }

private:
    EntitySpawnRequest(EntityType t, int32_t px, int32_t py, int32_t pz, PlayerId pid, uint32_t tick)
        : type(t), x(px), y(py), z(pz), playerId(pid), startTick(tick) {}
};

/**
 * @brief Function type for entity spawn implementation.
 *
 * Called during createEntities() to actually create the entity in the registry.
 */
using SpawnImplFn = std::function<entt::entity(entt::registry& reg,
                                                GlobalEntityId globalId,
                                                const EntitySpawnRequest& req)>;

/**
 * @brief Instantiable entity factory with deferred creation.
 *
 * Allows entities to be "spawned" (queued) at any time, but only actually
 * created in the registry when createEntities() is called at a precise
 * game engine step.
 *
 * Usage:
 *   EntityFactory factory;
 *   factory.registerSpawnImpl(EntityType::SHEEP, sheepSpawnImpl);
 *
 *   // Queue entities for later creation
 *   factory.spawn(EntityType::SHEEP, x, y, z);  // Non-player
 *   factory.spawnPlayer(EntityType::PLAYER, x, y, z, playerId);
 *
 *   // Later, at precise engine step:
 *   auto ids = factory.createEntities(registry, acquireEntityIdFn);
 */
class EntityFactory {
public:
    EntityFactory() = default;
    ~EntityFactory() = default;

    // Non-copyable, non-movable (contains function references that may capture this)
    EntityFactory(const EntityFactory&) = delete;
    EntityFactory& operator=(const EntityFactory&) = delete;
    EntityFactory(EntityFactory&&) = delete;
    EntityFactory& operator=(EntityFactory&&) = delete;

    /**
     * @brief Register a spawn implementation for an entity type.
     *
     * Must be called before spawn() can be used for the given type.
     */
    void registerSpawnImpl(EntityType type, SpawnImplFn fn);

    /**
     * @brief Queue a non-player entity for later creation.
     *
     * @param type Entity type (must have been registered)
     * @param x,y,z Spawn position in sub-voxels
     * @return The queued entity will be created when createEntities() is called
     */
    void spawn(EntityType type, int32_t x, int32_t y, int32_t z);

    /**
     * @brief Queue a player entity for later creation.
     *
     * @param type Entity type (must have been registered)
     * @param x,y,z Spawn position in sub-voxels
     * @param playerId Persistent player identifier
     */
    void spawnPlayer(EntityType type, int32_t x, int32_t y, int32_t z, PlayerId playerId);

    /**
     * @brief Queue an AI entity for later creation.
     *
     * @param type Entity type (must have been registered)
     * @param x,y,z Spawn position in sub-voxels
     * @param startTick Current tick (for AI state timing)
     */
    void spawnAI(EntityType type, int32_t x, int32_t y, int32_t z, uint32_t startTick);

    /**
     * @brief Queue a generic spawn request.
     */
    void spawnRequest(EntitySpawnRequest req);

    /**
     * @brief Create all queued entities in the registry.
     *
     * This is the ONLY method that actually touches the registry.
     * Should be called at a precise step of the game engine.
     *
     * After creation, entities are automatically added to their chunk's
     * entities set based on their spawn position.
     *
     * @param registry The ECS registry to create entities in
     * @param chunkRegistry Chunk registry for adding entities to chunks
     * @param acquireId Function that returns the next GlobalEntityId
     */
    void createEntities(
        entt::registry& registry,
        ChunkRegistry& chunkRegistry,
        const std::function<GlobalEntityId()>& acquireId);

    /**
     * @brief Check if there are pending spawn requests.
     */
    bool hasPendingSpawns() const { return !pending_.empty(); }

    /**
     * @brief Clear all pending spawn requests without creating them.
     */
    void clearPending() { pending_.clear(); }

    /**
     * @brief Get the number of pending spawn requests.
     */
    size_t pendingCount() const { return pending_.size(); }

private:
    std::unordered_map<EntityType, SpawnImplFn> spawnImpls_;
    std::vector<EntitySpawnRequest> pending_;
};

/**
 * @brief Create default-configured EntityFactory with all built-in types registered.
 *
 * This is the main entry point for production code.
 */
std::unique_ptr<EntityFactory> createDefaultEntityFactory();

} // namespace voxelmmo
