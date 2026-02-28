#pragma once
#include "common/Types.hpp"
#include "common/MessageTypes.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/DynamicPositionComponent.hpp"
#include <entt/entt.hpp>
#include <cstdint>
#include <cstring>

namespace voxelmmo {

/**
 * @brief Lightweight helper to write values sequentially into a pre-allocated byte buffer.
 */
struct BufWriter {
    uint8_t*  buf;
    size_t&   offset;
    uint32_t  currentTick; ///< Server tick used to advance entity state before serialising.

    /** @brief Append a trivially-copyable value and advance the offset. */
    template<typename T>
    void write(const T& v) noexcept {
        std::memcpy(buf + offset, &v, sizeof(T));
        offset += sizeof(T);
    }
};

/**
 * @brief Base entity wrapper – provides ECS-backed snapshot / delta serialisation.
 *
 * Derive from this class to implement entity-specific serialisation logic.
 * Each derived class must override serializeAllComponents() and
 * serializeDirtyComponents() to write the component data it owns.
 */
class BaseEntity {
public:
    entt::registry& registry;
    entt::entity    handle;

    EntityId   id;
    EntityType type;

    BaseEntity(entt::registry& reg, entt::entity ent,
               EntityId eid, EntityType etype)
        : registry(reg), handle(ent), id(eid), type(etype) {}

    virtual ~BaseEntity() = default;

    /**
     * @brief Serialise all components into buf starting at offset.
     *
     * Wire format: [ EntityId(uint16) | EntityType(uint8) | ComponentFlags(uint8)
     *                | ComponentStates... ]
     *
     * @param buf    Destination buffer (caller ensures enough space).
     * @param offset Read/write cursor, advanced by this call.
     */
    virtual void serializeSnapshot(uint8_t* buf, size_t& offset, uint32_t tickCount) const {
        BufWriter w{buf, offset, tickCount};
        w.write(id);
        w.write(static_cast<uint8_t>(type));
        serializeAllComponents(w);
    }

    /**
     * @brief Serialise only the components indicated by dirtyMask.
     *
     * Wire format: [ EntityId(uint16) | EntityType(uint8) | ComponentFlags(uint8)
     *                | DirtyComponentStates... ]
     *
     * @param buf       Destination buffer.
     * @param offset    Read/write cursor, advanced by this call.
     * @param dirtyMask Bitmask of dirty component bits (snapshotDirtyFlags or tickDirtyFlags).
     */
    virtual void serializeDelta(uint8_t* buf, size_t& offset, uint8_t dirtyMask, uint32_t tickCount) const {
        BufWriter w{buf, offset, tickCount};
        w.write(id);
        w.write(static_cast<uint8_t>(type));
        serializeDirtyComponents(w, dirtyMask);
    }

    /**
     * @brief Merge a newer serialised delta into an existing one (in-place).
     *
     * Components present in @p incoming overwrite the matching components in
     * @p current.  Components absent in @p incoming are left untouched.
     *
     * @param current  Existing delta buffer (modified in-place).
     * @param incoming Newer delta buffer to merge from.
     */
    static void appendSerializedDelta(uint8_t* current, const uint8_t* incoming);

    bool isSnapshotDirty() const {
        return registry.get<DirtyComponent>(handle).isSnapshotDirty();
    }
    bool isTickDirty() const {
        return registry.get<DirtyComponent>(handle).isTickDirty();
    }

protected:
    /** @brief Write ALL component values preceded by a full component-flags byte.
     *
     *  Position/velocity are advanced to w.currentTick via closed-form kinematics
     *  so the client can use the message-header tick as the prediction reference
     *  without needing a separate tick field in the component wire format.
     */
    virtual void serializeAllComponents(BufWriter& w) const {
        const auto& dyn = registry.get<DynamicPositionComponent>(handle);
        auto [px, py, pz] = DynamicPositionComponent::predictAt(dyn, w.currentTick);
        const float pvy   = DynamicPositionComponent::predictVy(dyn, w.currentTick);
        w.write<uint8_t>(POSITION_BIT);
        w.write(px);     w.write(py);     w.write(pz);
        w.write(dyn.vx); w.write(pvy);   w.write(dyn.vz);
        w.write<uint8_t>(dyn.grounded ? 1u : 0u);
    }

    /** @brief Write only the components indicated by dirtyMask. */
    virtual void serializeDirtyComponents(BufWriter& w, uint8_t dirtyMask) const {
        w.write(dirtyMask);
        if (dirtyMask & POSITION_BIT) {
            const auto& dyn = registry.get<DynamicPositionComponent>(handle);
            auto [px, py, pz] = DynamicPositionComponent::predictAt(dyn, w.currentTick);
            const float pvy   = DynamicPositionComponent::predictVy(dyn, w.currentTick);
            w.write(px);     w.write(py);     w.write(pz);
            w.write(dyn.vx); w.write(pvy);   w.write(dyn.vz);
            w.write<uint8_t>(dyn.grounded ? 1u : 0u);
        }
    }
};

} // namespace voxelmmo
