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
    server/game/WorldChunk.cpp
    server/game/WorldGenerator.cpp
    server/game/Chunk.cpp
    server/game/ChunkRegistry.cpp
    server/gateway/GatewayEngine.cpp
    server/gateway/StateManager.cpp
    server/game/entities/EntityFactory.cpp
    server/game/entities/PlayerEntity.cpp
    server/game/entities/GhostPlayerEntity.cpp
    server/game/entities/SheepEntity.cpp
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
    tests/server/test_world_gen.cpp
    tests/server/test_serialization.cpp
    tests/server/test_chunk_streaming.cpp
    tests/server/test_physics.cpp
    server/game/WorldChunk.cpp
    server/game/WorldGenerator.cpp
    server/game/Chunk.cpp
    server/game/ChunkRegistry.cpp
    server/game/GameEngine.cpp
    server/game/entities/EntityFactory.cpp
    server/game/entities/PlayerEntity.cpp
    server/game/entities/GhostPlayerEntity.cpp
    server/game/entities/SheepEntity.cpp
)

target_include_directories(voxelmmo_tests PRIVATE server/)

target_link_libraries(voxelmmo_tests PRIVATE
    Catch2::Catch2WithMain
    EnTT::EnTT
    lz4::lz4
)
