#pragma once
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include "common/EntityType.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>
#include <span>

// Forward declarations - use entt's fwd header if available, otherwise minimal forward decl
#include <entt/entity/fwd.hpp>

namespace voxelmmo {

class SafeBufWriter;
struct DirtyComponent;
struct EntitySpawnRequest;

using SerializeCreateFn = size_t(*)(entt::registry&, entt::entity, SafeBufWriter&);
using SerializeUpdateFn = size_t(*)(entt::registry&, entt::entity, const DirtyComponent&, SafeBufWriter&);
using SpawnImplFn = std::function<entt::entity(entt::registry&, GlobalEntityId, const EntitySpawnRequest&)>;

/**
 * @brief Spawn metadata for a registered entity type.
 */
struct EntitySpawnInfo {
    std::span<const VoxelType> spawnableVoxels;  ///< Voxel types this entity can spawn on
    float spawnProbabilityPerVoxel;              ///< Spawn probability per candidate voxel
};

/**
 * @brief Metadata for a registered entity type.
 */
struct EntityTypeInfo {
    uint8_t typeId;                    ///< Enum value (e.g., 3 for GOBLIN)
    std::string_view name;             ///< "GOBLIN"
    SerializeCreateFn serializeCreate; ///< Full state serialization
    SerializeUpdateFn serializeUpdate; ///< Delta state serialization
    SpawnImplFn spawnImpl;             ///< Factory spawn function
    EntitySpawnInfo spawnInfo;         ///< Spawn metadata
};

/**
 * @brief Central registry for all entity types.
 * 
 * Uses self-registration pattern: each entity type registers itself
 * via EntityRegistrar<T> static initialization.
 */
class EntityCatalog {
public:
    static EntityCatalog& instance();
    
    /**
     * @brief Register an entity type. Called by EntityRegistrar.
     */
    void registerType(const EntityTypeInfo& info);
    
    /**
     * @brief Find entity info by type ID.
     * @return Pointer to info, or nullptr if not found.
     */
    const EntityTypeInfo* findById(uint8_t typeId) const;
    
    /**
     * @brief Find entity info by name (case-insensitive).
     * @return Pointer to info, or nullptr if not found.
     */
    const EntityTypeInfo* findByName(std::string_view name) const;
    
    /**
     * @brief Get all registered types.
     */
    const std::vector<EntityTypeInfo>& allTypes() const { return types_; }
    
    /**
     * @brief Get serializeCreate function for a type.
     * @return Function pointer, or nullptr if not found.
     */
    SerializeCreateFn getSerializeCreate(uint8_t typeId) const;
    
    /**
     * @brief Get serializeUpdate function for a type.
     * @return Function pointer, or nullptr if not found.
     */
    SerializeUpdateFn getSerializeUpdate(uint8_t typeId) const;
    
    /**
     * @brief Get spawnImpl function for a type.
     * @return Function, or nullptr if not found.
     */
    SpawnImplFn getSpawnImpl(uint8_t typeId) const;
    
    /**
     * @brief Convert EntityType to human-readable string name.
     * @param type The entity type to convert.
     * @return String view containing the type name (e.g., "PLAYER", "SHEEP").
     *         Returns "UNKNOWN" for unrecognized types.
     */
    std::string_view typeToString(uint8_t typeId) const;
    
    /**
     * @brief Parse entity type from string name (case-insensitive).
     * @param str The string to parse (e.g., "sheep", "PLAYER").
     * @return Type ID if found, or std::nullopt if unrecognized.
     */
    std::optional<uint8_t> stringToType(std::string_view str) const;
    
    /**
     * @brief Get all entity types that can spawn on a specific voxel type.
     * @param voxel The voxel type to check.
     * @return Span of entity type IDs that can spawn on this voxel.
     */
    std::span<const EntityType> getSpawnableEntities(VoxelType voxel) const;
    
    /**
     * @brief Get spawn probability for an entity type.
     * @param typeId The entity type ID.
     * @return Probability per voxel, or 0.0f if not found or not spawnable.
     */
    float getSpawnProbability(uint8_t typeId) const;
    
    /**
     * @brief Check if a voxel type can spawn any entities.
     * @param voxel The voxel type to check.
     * @return true if at least one entity type can spawn on this voxel.
     */
    bool isSpawnableVoxel(VoxelType voxel) const;
    
private:
    EntityCatalog() = default;
    std::vector<EntityTypeInfo> types_;
    std::unordered_map<uint8_t, size_t> byId_;  ///< typeId -> index in types_
    
    // Cached mapping from voxel type to spawnable entity types
    mutable std::unordered_map<VoxelType, std::vector<EntityType>> voxelToEntities_;
    mutable bool spawnCacheBuilt_ = false;
    
    void buildSpawnCache() const;
};

/**
 * @brief Entity traits template - each entity type specializes this.
 * 
 * Example specialization:
 *   template<> struct EntityTraits<GoblinEntityTag> {
 *       static constexpr uint8_t typeId = 3;
 *       static constexpr std::string_view name = "GOBLIN";
 *       ...
 *   };
 */
template<typename T>
struct EntityTraits;

/**
 * @brief Registration helper - instantiated in entity .cpp files to auto-register.
 * 
 * Usage in EntityName.cpp:
 *   namespace { [[maybe_unused]] const auto& _reg = EntityRegistrar<EntityNameTag>{}; }
 */
template<typename EntityT>
struct EntityRegistrar {
    EntityRegistrar() {
        using Traits = EntityTraits<EntityT>;
        EntityCatalog::instance().registerType({
            Traits::typeId,
            Traits::name,
            Traits::serializeCreate,
            Traits::serializeUpdate,
            Traits::spawnImpl,
            {Traits::spawnableVoxels, Traits::spawnProbabilityPerVoxel}
        });
    }
};

} // namespace voxelmmo
