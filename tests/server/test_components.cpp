#include <catch2/catch_test_macros.hpp>
#include "game/components/DynamicPositionComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/components/BoundingBoxComponent.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/EntityTypeComponent.hpp"
#include "game/components/InputComponent.hpp"
#include "game/components/PhysicsModeComponent.hpp"

using namespace voxelmmo;

// ── DynamicPositionComponent Tests ───────────────────────────────────────────

TEST_CASE("DynamicPositionComponent default state", "[components]") {
    DynamicPositionComponent dyn;
    
    CHECK(dyn.x == 0);
    CHECK(dyn.y == 0);
    CHECK(dyn.z == 0);
    CHECK(dyn.vx == 0);
    CHECK(dyn.vy == 0);
    CHECK(dyn.vz == 0);
    CHECK(dyn.grounded == false);
    CHECK(dyn.moved == true);
}

TEST_CASE("DynamicPositionComponent::modify updates all fields", "[components]") {
    entt::registry reg;
    auto ent = reg.create();
    reg.emplace<DynamicPositionComponent>(ent);
    reg.emplace<DirtyComponent>(ent);
    
    DynamicPositionComponent::modify(reg, ent,
        100, 200, 300,      // position
        10, 20, 30,         // velocity
        true,               // grounded
        false               // not dirty
    );
    
    auto& dyn = reg.get<DynamicPositionComponent>(ent);
    CHECK(dyn.x == 100);
    CHECK(dyn.y == 200);
    CHECK(dyn.z == 300);
    CHECK(dyn.vx == 10);
    CHECK(dyn.vy == 20);
    CHECK(dyn.vz == 30);
    CHECK(dyn.grounded == true);
    CHECK(dyn.moved == true);
}

TEST_CASE("DynamicPositionComponent::modify marks dirty when requested", "[components]") {
    entt::registry reg;
    auto ent = reg.create();
    reg.emplace<DynamicPositionComponent>(ent);
    reg.emplace<DirtyComponent>(ent);
    
    DynamicPositionComponent::modify(reg, ent,
        0, 0, 0, 0, 0, 0, false, true  // dirty=true
    );
    
    auto& dirty = reg.get<DirtyComponent>(ent);
    CHECK(dirty.isSnapshotDirty());
    CHECK(dirty.isTickDirty());
}

TEST_CASE("DynamicPositionComponent::modify does not mark dirty when dirty=false", "[components]") {
    entt::registry reg;
    auto ent = reg.create();
    reg.emplace<DynamicPositionComponent>(ent);
    reg.emplace<DirtyComponent>(ent);
    
    DynamicPositionComponent::modify(reg, ent,
        0, 0, 0, 0, 0, 0, false, false  // dirty=false
    );
    
    auto& dirty = reg.get<DirtyComponent>(ent);
    CHECK_FALSE(dirty.isSnapshotDirty());
    CHECK_FALSE(dirty.isTickDirty());
}

TEST_CASE("DynamicPositionComponent::modify sets moved flag", "[components]") {
    entt::registry reg;
    auto ent = reg.create();
    reg.emplace<DynamicPositionComponent>(ent, 0, 0, 0, 0, 0, 0, false, false);
    reg.get<DynamicPositionComponent>(ent).moved = false;  // Clear moved
    reg.emplace<DirtyComponent>(ent);
    
    DynamicPositionComponent::modify(reg, ent,
        1, 0, 0, 0, 0, 0, false, false
    );
    
    auto& dyn = reg.get<DynamicPositionComponent>(ent);
    CHECK(dyn.moved == true);
}

TEST_CASE("DynamicPositionComponent serialization format", "[components]") {
    DynamicPositionComponent dyn;
    dyn.x = 0x01020304;
    dyn.y = 0x05060708;
    dyn.z = 0x090A0B0C;
    dyn.vx = 0x0D0E0F10;
    dyn.vy = 0x11121314;
    dyn.vz = 0x15161718;
    dyn.grounded = true;
    
    std::vector<uint8_t> buf;
    buf.resize(25);
    size_t offset = 0;
    BufWriter writer(buf.data(), offset);
    dyn.serializeFields(writer);
    
    // Little-endian format check
    CHECK(buf[0] == 0x04); CHECK(buf[1] == 0x03); CHECK(buf[2] == 0x02); CHECK(buf[3] == 0x01);
    CHECK(buf[24] == 0x01);  // grounded = 1
}

// ── DirtyComponent Tests ─────────────────────────────────────────────────────

TEST_CASE("DirtyComponent default state is clean", "[components]") {
    DirtyComponent dirty;
    
    CHECK_FALSE(dirty.isSnapshotDirty());
    CHECK_FALSE(dirty.isTickDirty());
    CHECK_FALSE(dirty.isCreated());
    CHECK_FALSE(dirty.hasComponentChanges());
}

TEST_CASE("DirtyComponent::mark sets both flags", "[components]") {
    DirtyComponent dirty;
    dirty.mark(POSITION_BIT);
    
    CHECK(dirty.isSnapshotDirty());
    CHECK(dirty.isTickDirty());
    CHECK(dirty.hasComponentChanges());
}

TEST_CASE("DirtyComponent::markCreated sets CREATED_BIT", "[components]") {
    DirtyComponent dirty;
    dirty.markCreated();
    
    CHECK(dirty.isCreated());
    CHECK(dirty.isSnapshotDirty());
}

TEST_CASE("DirtyComponent CREATED_BIT is distinct from component bits", "[components]") {
    DirtyComponent dirty;
    dirty.mark(POSITION_BIT);
    dirty.markCreated();
    
    CHECK(dirty.hasComponentChanges());  // Has component bit
    CHECK(dirty.isCreated());             // Has lifecycle bit
}

TEST_CASE("DirtyComponent::clearSnapshot clears snapshot but not tick", "[components]") {
    DirtyComponent dirty;
    dirty.mark(POSITION_BIT);
    
    dirty.clearSnapshot();
    
    CHECK_FALSE(dirty.isSnapshotDirty());
    CHECK(dirty.isTickDirty());  // Preserved
}

TEST_CASE("DirtyComponent::clearTick clears tick but not snapshot", "[components]") {
    DirtyComponent dirty;
    dirty.mark(POSITION_BIT);
    
    dirty.clearTick();
    
    CHECK(dirty.isSnapshotDirty());  // Preserved
    CHECK_FALSE(dirty.isTickDirty());
}

TEST_CASE("DirtyComponent hasComponentChanges ignores lifecycle bits", "[components]") {
    DirtyComponent dirty;
    dirty.markCreated();  // Lifecycle bit only
    
    CHECK_FALSE(dirty.hasComponentChanges());
}

// ── BoundingBoxComponent Tests ───────────────────────────────────────────────

TEST_CASE("BoundingBoxComponent default is zero", "[components]") {
    BoundingBoxComponent bbox;
    
    CHECK(bbox.hx == 0);
    CHECK(bbox.hy == 0);
    CHECK(bbox.hz == 0);
}

TEST_CASE("BoundingBoxComponent stores half-extents", "[components]") {
    BoundingBoxComponent bbox{128, 256, 64};
    
    CHECK(bbox.hx == 128);
    CHECK(bbox.hy == 256);
    CHECK(bbox.hz == 64);
}

// ── GlobalEntityIdComponent Tests ────────────────────────────────────────────

TEST_CASE("GlobalEntityIdComponent stores ID", "[components]") {
    GlobalEntityIdComponent gid{12345};
    CHECK(gid.id == 12345);
}

// ── EntityTypeComponent Tests ─────────────────────────────────────────────────

TEST_CASE("EntityTypeComponent stores type", "[components]") {
    EntityTypeComponent etc{EntityType::PLAYER};
    CHECK(etc.type == EntityType::PLAYER);
}

// ── InputComponent Tests ─────────────────────────────────────────────────────

TEST_CASE("InputComponent default state", "[components]") {
    InputComponent input;
    
    CHECK(input.buttons == 0);
    CHECK(input.yaw == 0.0f);
    CHECK(input.pitch == 0.0f);
}

TEST_CASE("InputComponent stores all fields", "[components]") {
    InputComponent input;
    input.buttons = 0x1F;
    input.yaw = 3.14159f;
    input.pitch = -0.5f;
    
    CHECK(input.buttons == 0x1F);
    CHECK(input.yaw == 3.14159f);
    CHECK(input.pitch == -0.5f);
}

// ── PhysicsModeComponent Tests ────────────────────────────────────────────────

TEST_CASE("PhysicsModeComponent stores mode", "[components]") {
    PhysicsModeComponent pmc{PhysicsMode::FULL};
    CHECK(pmc.mode == PhysicsMode::FULL);
    
    pmc.mode = PhysicsMode::GHOST;
    CHECK(pmc.mode == PhysicsMode::GHOST);
    
    pmc.mode = PhysicsMode::FLYING;
    CHECK(pmc.mode == PhysicsMode::FLYING);
}
