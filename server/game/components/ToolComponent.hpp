#pragma once
#include "common/ToolCatalog.hpp"
#include "common/SafeBufWriter.hpp"
#include "game/components/DirtyComponent.hpp"
#include <entt/entt.hpp>
#include <cstdint>

namespace voxelmmo {

// Component dirty bit (must match client)
inline constexpr uint8_t TOOL_BIT = 1 << 3;

/**
 * @brief Component for entities that can hold and use tools.
 *
 * Tracks current tool type and cooldown state.
 * Default tool is HAND (0).
 */
struct ToolComponent {
    uint8_t toolId{static_cast<uint8_t>(ToolType::HAND)};  ///< Current tool type
    uint32_t lastUsedTick{0};                              ///< Tick when tool was last used

    /**
     * @brief Check if tool can be used (cooldown expired).
     * @param currentTick Current game tick.
     * @param catalog Tool catalog for cooldown lookup.
     * @return true if cooldown has expired.
     */
    bool canUse(uint32_t currentTick, const ToolCatalog& catalog) const {
        const auto* info = catalog.findById(toolId);
        if (!info) return false;
        return currentTick >= lastUsedTick + info->cooldownTicks;
    }

    /**
     * @brief Mark tool as used, setting lastUsedTick.
     * @param tick Current game tick.
     */
    void markUsed(uint32_t tick) { lastUsedTick = tick; }

    /**
     * @brief Check if component differs from default values.
     */
    bool isNonDefault() const noexcept {
        return toolId != static_cast<uint8_t>(ToolType::HAND) || lastUsedTick != 0;
    }

    /**
     * @brief Serialize tool fields.
     * Wire layout: uint8 toolId | uint32 lastUsedTick
     */
    void serializeFields(SafeBufWriter& w) const noexcept {
        w.write(toolId);
        w.write(lastUsedTick);
    }

    /**
     * @brief Modify tool and mark dirty.
     * @param reg Entity registry.
     * @param ent Entity handle.
     * @param newToolId New tool type ID.
     * @param dirty Whether to mark TOOL_BIT dirty.
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       uint8_t newToolId, bool dirty = true) {
        auto& c = reg.get<ToolComponent>(ent);
        c.toolId = newToolId;
        if (dirty) {
            reg.get<DirtyComponent>(ent).mark(TOOL_BIT);
        }
    }

    /**
     * @brief Modify tool usage time.
     * @param reg Entity registry.
     * @param ent Entity handle.
     * @param tick Current tick (sets lastUsedTick).
     * @param dirty Whether to mark TOOL_BIT dirty.
     */
    static void markUsed(entt::registry& reg, entt::entity ent,
                         uint32_t tick, bool dirty = true) {
        auto& c = reg.get<ToolComponent>(ent);
        c.lastUsedTick = tick;
        if (dirty) {
            reg.get<DirtyComponent>(ent).mark(TOOL_BIT);
        }
    }
};

} // namespace voxelmmo
