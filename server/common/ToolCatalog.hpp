#pragma once
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>

namespace voxelmmo {

/**
 * @brief Tool type enumeration.
 */
enum class ToolType : uint8_t {
    HAND = 0,           // Default unarmed attack
    SELECT_VOXEL = 1,   // Voxel selection tool (destroy/copy/paste)
    CREATE_VOXEL = 2,   // Voxel creation tool
    NONE = 255,         // No tool selected
    COUNT = 3
};

/**
 * @brief Metadata for a registered tool type.
 */
struct ToolInfo {
    uint8_t toolId;              ///< ToolType enum value
    std::string_view name;       ///< "HAND", etc.
    uint16_t damage;             ///< Base damage amount
    uint32_t cooldownTicks;      ///< Cooldown between uses (at 20tps)
    float range;                 ///< Hit range in sub-voxels (e.g., 768 = 3 voxels)
    float knockback;             ///< Velocity impulse (sub-voxels/tick)
};

/**
 * @brief Central registry for all tool types.
 *
 * Singleton pattern - use ToolCatalog::instance() to access.
 */
class ToolCatalog {
public:
    static ToolCatalog& instance();

    /**
     * @brief Register a tool type.
     */
    void registerTool(const ToolInfo& info);

    /**
     * @brief Find tool info by tool ID.
     * @return Pointer to info, or nullptr if not found.
     */
    const ToolInfo* findById(uint8_t toolId) const;

    /**
     * @brief Find tool info by name (case-insensitive).
     * @return Pointer to info, or nullptr if not found.
     */
    const ToolInfo* findByName(std::string_view name) const;

    /**
     * @brief Convert tool type to human-readable string.
     */
    std::string_view typeToString(uint8_t toolId) const;

    /**
     * @brief Parse tool type from string (case-insensitive).
     */
    std::optional<uint8_t> stringToType(std::string_view str) const;

    /**
     * @brief Get all registered tools.
     */
    const std::vector<ToolInfo>& allTools() const { return tools_; }

private:
    ToolCatalog();
    ~ToolCatalog() = default;
    ToolCatalog(const ToolCatalog&) = delete;
    ToolCatalog& operator=(const ToolCatalog&) = delete;

    std::vector<ToolInfo> tools_;
    std::unordered_map<uint8_t, size_t> byId_;  ///< toolId -> index in tools_

    void registerDefaults();  ///< Register built-in tools (HAND)
};

/**
 * @brief Helper to get default ToolInfo for a tool type.
 * @return Default ToolInfo for the type.
 */
ToolInfo makeDefaultToolInfo(ToolType type);

} // namespace voxelmmo
