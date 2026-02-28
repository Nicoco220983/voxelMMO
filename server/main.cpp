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

    gateway.setPlayerConnectCallback([&](voxelmmo::PlayerId pid) {
        const voxelmmo::EntityId eid = game.addPlayer(0, pid, 32.0f, 20.0f, 32.0f);
        std::cout << "[main] Player " << pid << " → entity " << eid << "\n";
        game.sendSnapshot(0);
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
