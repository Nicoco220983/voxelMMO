#include "common/VoxelCatalog.hpp"
#include "common/VoxelTypes.hpp"
#include <cctype>

namespace voxelmmo {

namespace {

/**
 * @brief Mapping from VoxelType to VoxelPhysicType.
 *
 * O(1) lookup table. Size is 256 now (fits uint8), expandable if VoxelType grows.
 * Most voxels map to SOLID physics type.
 */
std::array<VoxelPhysicType, 256> makeVoxelToPhysicTable() {
    std::array<VoxelPhysicType, 256> table{};
    table.fill(VoxelPhysicTypes::SOLID);  // default

    table[VoxelTypes::AIR]         = VoxelPhysicTypes::AIR;
    table[VoxelTypes::SLIME]       = VoxelPhysicTypes::SLIME;
    table[VoxelTypes::MUD]         = VoxelPhysicTypes::MUD;
    table[VoxelTypes::LADDER]      = VoxelPhysicTypes::LADDER;
    table[VoxelTypes::GOBLIN_BED]  = VoxelPhysicTypes::AIR;  // Non-solid, passable

    return table;
}

const std::array<VoxelPhysicType, 256> VOXEL_TO_PHYSIC = makeVoxelToPhysicTable();

} // anonymous namespace

VoxelCatalog& VoxelCatalog::instance() {
    static VoxelCatalog catalog;
    return catalog;
}

void VoxelCatalog::registerType(const VoxelTypeInfo& info) {
    // Check if already registered
    if (byId_.find(info.typeId) != byId_.end()) {
        return;  // Already registered, skip
    }

    size_t index = types_.size();
    types_.push_back(info);
    byId_[info.typeId] = index;
}

const VoxelTypeInfo* VoxelCatalog::findById(VoxelType typeId) const {
    auto it = byId_.find(typeId);
    if (it != byId_.end()) {
        return &types_[it->second];
    }
    return nullptr;
}

const VoxelTypeInfo* VoxelCatalog::findByName(std::string_view name) const {
    // Case-insensitive comparison against voxel names
    for (const auto& info : types_) {
        if (info.name.size() != name.size()) continue;

        bool match = true;
        for (size_t i = 0; i < name.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(info.name[i])) !=
                std::tolower(static_cast<unsigned char>(name[i]))) {
                match = false;
                break;
            }
        }
        if (match) return &info;
    }
    return nullptr;
}

VoxelActivateFn VoxelCatalog::getOnActivate(VoxelType typeId) const {
    auto* info = findById(typeId);
    return info ? info->onActivate : nullptr;
}

bool VoxelCatalog::hasActivateCallback(VoxelType typeId) const {
    auto* info = findById(typeId);
    return info && info->onActivate != nullptr;
}

std::string_view VoxelCatalog::typeToString(VoxelType type) const {
    const auto* info = findById(type);
    return info ? info->name : "UNKNOWN";
}

std::optional<VoxelType> VoxelCatalog::stringToType(std::string_view str) const {
    const auto* info = findByName(str);
    return info ? std::optional<VoxelType>(info->typeId) : std::nullopt;
}

VoxelPhysicType toVoxelPhysicType(VoxelType vt) {
    return VOXEL_TO_PHYSIC[vt];
}

} // namespace voxelmmo
