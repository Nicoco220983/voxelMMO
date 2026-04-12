#include "common/EntityCatalog.hpp"
#include <cctype>

namespace voxelmmo {

EntityCatalog& EntityCatalog::instance() {
    static EntityCatalog catalog;
    return catalog;
}

void EntityCatalog::registerType(const EntityTypeInfo& info) {
    // Check if already registered
    if (byId_.find(info.typeId) != byId_.end()) {
        return;  // Already registered, skip
    }
    
    size_t index = types_.size();
    types_.push_back(info);
    byId_[info.typeId] = index;
}

const EntityTypeInfo* EntityCatalog::findById(uint8_t typeId) const {
    auto it = byId_.find(typeId);
    if (it != byId_.end()) {
        return &types_[it->second];
    }
    return nullptr;
}

const EntityTypeInfo* EntityCatalog::findByName(std::string_view name) const {
    // Case-insensitive comparison against entity names
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

SerializeCreateFn EntityCatalog::getSerializeCreate(uint8_t typeId) const {
    auto* info = findById(typeId);
    return info ? info->serializeCreate : nullptr;
}

SerializeUpdateFn EntityCatalog::getSerializeUpdate(uint8_t typeId) const {
    auto* info = findById(typeId);
    return info ? info->serializeUpdate : nullptr;
}

SpawnImplFn EntityCatalog::getSpawnImpl(uint8_t typeId) const {
    auto* info = findById(typeId);
    return info ? info->spawnImpl : nullptr;
}

std::string_view EntityCatalog::typeToString(uint8_t typeId) const {
    const auto* info = findById(typeId);
    return info ? info->name : "UNKNOWN";
}

std::optional<uint8_t> EntityCatalog::stringToType(std::string_view str) const {
    const auto* info = findByName(str);
    return info ? std::optional<uint8_t>(info->typeId) : std::nullopt;
}

} // namespace voxelmmo
