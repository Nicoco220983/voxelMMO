cmake_minimum_required(VERSION 3.20)
project(voxelmmo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies (provided via vcpkg toolchain) ──────────────────────────────
find_package(EnTT                      CONFIG REQUIRED)
find_package(lz4                       CONFIG REQUIRED)
find_package(unofficial-uwebsockets    CONFIG REQUIRED)

# ── Single server executable (game + gateway in one process by default) ──────
add_executable(voxelmmo
    server/main.cpp
    server/game/GameEngine.cpp
    server/game/ChunkSerializer.cpp
    server/game/WorldChunk.cpp
    server/game/WorldGenerator.cpp
    server/game/ChunkRegistry.cpp
    server/game/SaveSystem.cpp
    server/game/EntitySerializer.cpp
    server/game/EntityTypeSerializers.cpp
    server/gateway/GatewayEngine.cpp
    server/game/entities/EntityFactory.cpp
    server/game/entities/PlayerEntity.cpp
    server/game/entities/GhostPlayerEntity.cpp
    server/game/entities/SheepEntity.cpp
    server/game/systems/ChunkMembershipSystem.cpp
    server/game/systems/DisconnectedPlayerSystem.cpp
    server/game/systems/InputSystem.cpp
    server/game/systems/PhysicsSystem.cpp
    server/game/systems/JumpSystem.cpp
    server/game/systems/SheepAISystem.cpp
    server/game/components/GroundContactComponent.cpp
    server/game/components/WalkComponent.cpp
)

target_include_directories(voxelmmo PRIVATE server/)

target_link_libraries(voxelmmo PRIVATE
    EnTT::EnTT
    lz4::lz4
    unofficial::uwebsockets::uwebsockets
)

# Optional: higher optimization for Release builds
target_compile_options(voxelmmo PRIVATE
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# ── Tests (Catch2) ────────────────────────────────────────────────────────────
find_package(Catch2 3 CONFIG REQUIRED)

add_executable(voxelmmo_tests
    # Test utilities
    tests/server/TestUtils.cpp
    # Test files
    tests/server/test_world_gen.cpp
    tests/server/test_network_protocol.cpp
    tests/server/test_protocol_cross_platform.cpp
    tests/server/test_components.cpp
    tests/server/test_physics.cpp
    tests/server/test_game_engine.cpp
    tests/server/test_save_system.cpp
    tests/server/test_save_integration.cpp
    # Server implementation files
    server/game/WorldChunk.cpp
    server/game/WorldGenerator.cpp
    server/game/ChunkSerializer.cpp
    server/game/ChunkRegistry.cpp
    server/game/SaveSystem.cpp
    server/game/GameEngine.cpp
    server/game/EntitySerializer.cpp
    server/game/EntityTypeSerializers.cpp
    server/game/entities/EntityFactory.cpp
    server/game/entities/PlayerEntity.cpp
    server/game/entities/GhostPlayerEntity.cpp
    server/game/entities/SheepEntity.cpp
    server/game/systems/ChunkMembershipSystem.cpp
    server/game/systems/DisconnectedPlayerSystem.cpp
    server/game/systems/InputSystem.cpp
    server/game/systems/PhysicsSystem.cpp
    server/game/systems/JumpSystem.cpp
    server/game/systems/SheepAISystem.cpp
    server/game/components/GroundContactComponent.cpp
    server/game/components/WalkComponent.cpp
)

target_include_directories(voxelmmo_tests PRIVATE server/ tests/server/)

target_link_libraries(voxelmmo_tests PRIVATE
    Catch2::Catch2WithMain
    EnTT::EnTT
    lz4::lz4
)
