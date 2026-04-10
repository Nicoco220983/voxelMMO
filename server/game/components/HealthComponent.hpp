#pragma once
#include "DirtyComponent.hpp"
#include "PendingDeleteComponent.hpp"
#include "common/Types.hpp"
#include "common/SafeBufWriter.hpp"
#include <entt/entt.hpp>
#include <cstdint>

namespace voxelmmo {

inline constexpr uint8_t HEALTH_BIT = 1 << 2;

/**
 * @brief Health component for damageable entities (players, sheep, etc.).
 *
 * Tracks current/max health and the tick when last damage was taken.
 * When current health reaches 0, PendingDeleteComponent is automatically added
 * with a TTL delay for client death feedback.
 *
 * Serialized layout (when HEALTH_BIT is set):
 *   uint16 current | uint16 max | uint32 lastDamageTick
 */
struct HealthComponent {
    uint16_t current{0};        ///< Current health points
    uint16_t max{0};            ///< Maximum health points
    uint32_t lastDamageTick{0}; ///< Tick when health last changed (damage or heal)

    /**
     * @brief Check if health is at non-default values.
     * Used by serializeCreate to determine if component needs to be sent.
     */
    bool isNonDefault() const noexcept {
        return current != 0 || max != 0;
    }

    /**
     * @brief Serialize the health fields (no component-flags byte).
     *
     * Wire layout: uint16 current | uint16 max | uint32 lastDamageTick
     * The caller is responsible for writing the component-flags byte beforehand.
     */
    void serializeFields(SafeBufWriter& w) const noexcept {
        w.write(current);
        w.write(max);
        w.write(lastDamageTick);
    }

    /**
     * @brief Modify health and mark dirty.
     * @param reg      Entity registry
     * @param ent      Entity handle
     * @param newCurrent  New current health value
     * @param newMax      New max health value (usually unchanged)
     * @param tick        Current game tick (for lastDamageTick)
     * @param dirty       Whether to mark HEALTH_BIT dirty (true for damage/heal)
     */
    static void modify(entt::registry& reg, entt::entity ent,
                       uint16_t newCurrent, uint16_t newMax,
                       uint32_t tick, bool dirty = true) {
        auto& c = reg.get<HealthComponent>(ent);
        c.current = newCurrent;
        c.max = newMax;
        c.lastDamageTick = tick;
        if (dirty) {
            reg.get<DirtyComponent>(ent).mark(HEALTH_BIT);
        }
    }

    /**
     * @brief Apply damage to the entity.
     * If health reaches 0, automatically marks entity for delayed deletion.
     * @param reg      Entity registry
     * @param ent      Entity handle
     * @param amount   Damage amount
     * @param tick     Current game tick
     * @return true if entity died (health reached 0), false otherwise
     */
    static bool applyDamage(entt::registry& reg, entt::entity ent,
                            uint16_t amount, uint32_t tick) {
        auto& c = reg.get<HealthComponent>(ent);
        uint16_t newHealth = (amount >= c.current) ? 0 : c.current - amount;
        modify(reg, ent, newHealth, c.max, tick, /*dirty=*/true);
        
        const bool died = newHealth == 0;
        if (died) {
            // Mark for delayed deletion (allows client death animation/feedback)
            if (!reg.all_of<PendingDeleteComponent>(ent)) {
                reg.emplace<PendingDeleteComponent>(ent, tick + DEATH_DELETION_DELAY_TICKS);
            }
        }
        return died;
    }

    /**
     * @brief Serialize HealthComponent if HEALTH_BIT is set in flags.
     *
     * Helper for entity-type-specific serializers.
     *
     * @param reg   Entity registry
     * @param ent   Entity handle
     * @param flags Component mask
     * @param w     Buffer writer
     */
    static void serialize(
        entt::registry& reg,
        entt::entity ent,
        uint8_t flags,
        SafeBufWriter& w) {
        if (flags & HEALTH_BIT) {
            const auto& health = reg.get<HealthComponent>(ent);
            health.serializeFields(w);
        }
    }
};

} // namespace voxelmmo
