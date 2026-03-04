#include "game/GameEngine.hpp"
#include "gateway/GatewayEngine.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

static constexpr int GATEWAY_PORT = 8080;

int main() {
    voxelmmo::GameEngine    game;
    voxelmmo::GatewayEngine gateway;

    // ── Wire GameEngine → GatewayEngine ──────────────────────────────────
    game.setOutputCallback(
        [&](voxelmmo::GatewayId /*gwId*/, const uint8_t* data, size_t size) {
            gateway.receiveGameBatch(data, size);
        });

    // ── Wire GatewayEngine → GameEngine ──────────────────────────────────
    game.registerGateway(0);

    // Compute spawn position: one voxel above the terrain surface at (32, 32).
    // surfaceY ∈ [4, 30]; +2 places the player centre at surfaceY+2 so the
    // AABB bottom (centre − PLAYER_BBOX_HY ≈ 0.9 vox) clears the surface.
    static constexpr int32_t SPAWN_X = 32 * voxelmmo::SUBVOXEL_SIZE;
    static constexpr int32_t SPAWN_Z = 32 * voxelmmo::SUBVOXEL_SIZE;
    const int32_t spawnY = (game.getWorldGenerator().surfaceY(32, 32) + 2) * voxelmmo::SUBVOXEL_SIZE;

    gateway.setPlayerConnectCallback([&, spawnY](voxelmmo::PlayerId pid) {
        game.queuePendingPlayer(0, pid, SPAWN_X, spawnY, SPAWN_Z);
        std::cout << "[main] Player " << pid << " connected (pending JOIN)\n";
        // Entity is spawned when the client sends JOIN; sendSnapshot() is called then.
    });

    gateway.setPlayerDisconnectCallback([&](voxelmmo::PlayerId pid) {
        game.removePlayer(pid);
    });

    gateway.setPlayerInputCallback([&](voxelmmo::PlayerId pid,
                                       const uint8_t* data, size_t size) {
        game.handlePlayerInput(pid, data, size);
    });

    // ── Game loop (separate thread) ───────────────────────────────────────
    std::atomic<bool> running{true};
    std::thread gameThread([&]() {
        using Clock = std::chrono::steady_clock;
        const auto tickDuration = std::chrono::milliseconds(1000 / voxelmmo::TICK_RATE);
        auto nextTick = Clock::now();

        while (running) {
            nextTick += tickDuration;
            game.tick();
            std::this_thread::sleep_until(nextTick);
        }
    });

    // ── Gateway (blocks main thread with uWS event loop) ─────────────────
    std::cout << "[main] Starting voxelmmo server...\n";
    gateway.listen(GATEWAY_PORT); // blocks

    running = false;
    gameThread.join();
    return 0;
}
