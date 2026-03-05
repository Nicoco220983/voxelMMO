#pragma once
#include <cstdint>
#include <string_view>
#include <string>
#include <unordered_map>

namespace voxelmmo {

/**
 * @brief Registered entity types.
 * 
 * This enum defines all entity types in the game. The values are used in
 * network protocol messages and must stay in sync with client/src/types.js
 */
enum class EntityType : uint8_t {
    PLAYER       = 0,  ///< Full-physics player (gravity + collision)
    GHOST_PLAYER = 1,  ///< Ghost player (noclip, no gravity)
    SHEEP        = 2,  ///< Passive mob: wanders randomly, blocked by voxels
};

/** @brief Maps EntityType to string name (e.g., EntityType::SHEEP → "SHEEP"). */
inline const std::unordered_map<EntityType, std::string_view> ENTITY_TYPE_NAMES = {
    {EntityType::PLAYER,       "PLAYER"},
    {EntityType::GHOST_PLAYER, "GHOST_PLAYER"},
    {EntityType::SHEEP,        "SHEEP"},
};

/** @brief Maps lowercase string to EntityType (case-insensitive lookup). */
inline const std::unordered_map<std::string_view, EntityType> ENTITY_TYPE_BY_NAME = {
    {"player",       EntityType::PLAYER},
    {"ghost_player", EntityType::GHOST_PLAYER},
    {"sheep",        EntityType::SHEEP},
};

/**
 * @brief Convert EntityType to human-readable string name.
 * @param type The entity type to convert.
 * @return String view containing the type name (e.g., "PLAYER", "SHEEP").
 *         Returns "UNKNOWN" for unrecognized types.
 */
inline std::string_view entityTypeToString(EntityType type) {
    auto it = ENTITY_TYPE_NAMES.find(type);
    return it != ENTITY_TYPE_NAMES.end() ? it->second : "UNKNOWN";
}

/**
 * @brief Parse entity type from string name (case-insensitive).
 * @param str The string to parse (e.g., "sheep", "PLAYER").
 * @param outType Output parameter receiving the parsed type.
 * @return true if parsing succeeded, false if string is unrecognized.
 */
inline bool stringToEntityType(std::string_view str, EntityType& outType) {
    // Convert to lowercase for lookup
    std::string lower;
    lower.reserve(str.size());
    for (char c : str) {
        lower.push_back(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    auto it = ENTITY_TYPE_BY_NAME.find(lower);
    if (it != ENTITY_TYPE_BY_NAME.end()) {
        outType = it->second;
        return true;
    }
    return false;
}

} // namespace voxelmmo
