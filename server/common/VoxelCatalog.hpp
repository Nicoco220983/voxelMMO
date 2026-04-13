#pragma once
#include "common/Types.hpp"
#include "common/VoxelTypes.hpp"
#include "common/VoxelPhysicType.hpp"
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>
#include <optional>
#include <array>

namespace voxelmmo {

class Chunk;
class EntityFactory;

// Forward declarations from other headers
struct EntitySpawnRequest;

/**
 * @brief Callback function type for voxel activation.
 *
 * Called for each voxel of this type when a chunk is activated.
 * The callback handles a single voxel instance (e.g., spawns an entity above it).
 *
 * @param chunkId The activated chunk ID
 * @param chunk The chunk data (read-only access to voxels)
 * @param x Local X coordinate within chunk [0, CHUNK_SIZE_X)
 * @param y Local Y coordinate within chunk [0, CHUNK_SIZE_Y)
 * @param z Local Z coordinate within chunk [0, CHUNK_SIZE_Z)
 * @param entityFactory Factory for spawning entities
 * @param tick Current server tick
 */
using VoxelActivateFn = std::function<void(ChunkId, const Chunk&, int x, int y, int z, EntityFactory&, uint32_t)>;

/**
 * @brief Metadata for a registered voxel type.
 */
struct VoxelTypeInfo {
    VoxelType typeId;                       ///< Voxel type value (e.g., 9 for GOBLIN_BED)
    std::string_view name;                  ///< "GOBLIN_BED"
    VoxelActivateFn onActivate;   ///< Optional callback for voxel activation (nullptr if none)
};

/**
 * @brief Central registry for voxel type metadata and behaviors.
 *
 * Similar to EntityCatalog but for voxels. Uses self-registration pattern
 * via VoxelRegistrar<T> static initialization.
 */
class VoxelCatalog {
public:
    static VoxelCatalog& instance();

    /**
     * @brief Register a voxel type. Called by VoxelRegistrar.
     */
    void registerType(const VoxelTypeInfo& info);

    /**
     * @brief Find voxel info by type ID.
     * @return Pointer to info, or nullptr if not found.
     */
    const VoxelTypeInfo* findById(VoxelType typeId) const;

    /**
     * @brief Find voxel info by name (case-insensitive).
     * @return Pointer to info, or nullptr if not found.
     */
    const VoxelTypeInfo* findByName(std::string_view name) const;

    /**
     * @brief Get all registered types.
     */
    const std::vector<VoxelTypeInfo>& allTypes() const { return types_; }

    /**
     * @brief Get the onActivate callback for a voxel type.
     * @return Function, or nullptr if not found or no callback registered.
     */
    VoxelActivateFn getOnActivate(VoxelType typeId) const;

    /**
     * @brief Check if a voxel type has a voxel activation callback.
     * @return true if the voxel type has an onActivate callback.
     */
    bool hasActivateCallback(VoxelType typeId) const;

    /**
     * @brief Convert VoxelType to human-readable string name.
     * @param type The voxel type to convert.
     * @return String view containing the type name (e.g., "STONE", "GOBLIN_BED").
     *         Returns "UNKNOWN" for unrecognized types.
     */
    std::string_view typeToString(VoxelType type) const;

    /**
     * @brief Parse voxel type from string name (case-insensitive).
     * @param str The string to parse (e.g., "stone", "GOBLIN_BED").
     * @return Type ID if found, or std::nullopt if unrecognized.
     */
    std::optional<VoxelType> stringToType(std::string_view str) const;

private:
    VoxelCatalog() = default;
    std::vector<VoxelTypeInfo> types_;
    std::unordered_map<VoxelType, size_t> byId_;  ///< typeId -> index in types_
};

/**
 * @brief Voxel traits template - each voxel type specializes this.
 *
 * Example specialization:
 *   template<> struct VoxelTraits<GoblinBedTag> {
 *       static constexpr VoxelType typeId = VoxelTypes::GOBLIN_BED;
 *       static constexpr std::string_view name = "GOBLIN_BED";
 *       static constexpr auto onActivate = GoblinBedVoxel::onActivate;
 *   };
 */
template<typename T>
struct VoxelTraits;

/**
 * @brief Registration helper - instantiated in voxel .cpp files to auto-register.
 *
 * Usage in VoxelName.cpp:
 *   namespace { [[maybe_unused]] const auto& _reg = VoxelRegistrar<VoxelNameTag>{}; }
 */
template<typename VoxelT>
struct VoxelRegistrar {
    VoxelRegistrar() {
        using Traits = VoxelTraits<VoxelT>;
        VoxelCatalog::instance().registerType({
            Traits::typeId,
            Traits::name,
            Traits::onActivate
        });
    }
};

/**
 * @brief Get the physics type for a voxel type.
 * O(1), inline, no branching.
 */
VoxelPhysicType toVoxelPhysicType(VoxelType vt);

} // namespace voxelmmo
