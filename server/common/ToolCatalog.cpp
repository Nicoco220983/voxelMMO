#include "common/ToolCatalog.hpp"
#include <cctype>

namespace voxelmmo {

ToolCatalog& ToolCatalog::instance() {
    static ToolCatalog catalog;
    return catalog;
}

ToolCatalog::ToolCatalog() {
    registerDefaults();
}

void ToolCatalog::registerDefaults() {
    // Register default Hand tool
    registerTool(makeDefaultToolInfo(ToolType::HAND));
}

ToolInfo makeDefaultToolInfo(ToolType type) {
    switch (type) {
        case ToolType::HAND:
            return ToolInfo{
                static_cast<uint8_t>(ToolType::HAND),
                "HAND",
                5,          // 5 damage
                10,         // 0.5 second cooldown at 20tps
                768.0f,     // 3 voxels range (3 * 256)
                200.0f      // knockback impulse
            };
        default:
            return ToolInfo{
                static_cast<uint8_t>(ToolType::NONE),
                "NONE",
                0, 0, 0.0f, 0.0f
            };
    }
}

void ToolCatalog::registerTool(const ToolInfo& info) {
    // Check if already registered
    if (byId_.find(info.toolId) != byId_.end()) {
        return;  // Already registered, skip
    }

    size_t index = tools_.size();
    tools_.push_back(info);
    byId_[info.toolId] = index;
}

const ToolInfo* ToolCatalog::findById(uint8_t toolId) const {
    auto it = byId_.find(toolId);
    if (it != byId_.end()) {
        return &tools_[it->second];
    }
    return nullptr;
}

const ToolInfo* ToolCatalog::findByName(std::string_view name) const {
    // Case-insensitive comparison
    for (const auto& info : tools_) {
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

std::string_view ToolCatalog::typeToString(uint8_t toolId) const {
    const auto* info = findById(toolId);
    return info ? info->name : "UNKNOWN";
}

std::optional<uint8_t> ToolCatalog::stringToType(std::string_view str) const {
    const auto* info = findByName(str);
    return info ? std::optional<uint8_t>(info->toolId) : std::nullopt;
}

} // namespace voxelmmo
