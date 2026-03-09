#include "game/GameEngine.hpp"
#include "gateway/GatewayEngine.hpp"
#include "common/EntityType.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <optional>

static constexpr int GATEWAY_PORT = 8080;

static void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --seed <n>                World generation seed (default: random)\n"
              << "  --type <normal|test>      World generation type (default: normal)\n"
              << "  --test-entity-type <type> Entity type for TEST mode (optional)\n"
              << "                            Available types: PLAYER, GHOST_PLAYER, SHEEP\n"
              << "                            If not specified, no entities spawn (player-only)\n"
              << "  --help                    Show this help message\n";
}

int main(int argc, char* argv[]) {
    // ── Parse CLI arguments ────────────────────────────────────────────────
    uint32_t seed = 0;
    bool seedProvided = false;
    voxelmmo::GeneratorType genType = voxelmmo::GeneratorType::NORMAL;
    std::optional<voxelmmo::EntityType> testEntityType;
    
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        else if (std::strcmp(arg, "--seed") == 0 && i + 1 < argc) {
            seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            seedProvided = true;
        }
        else if (std::strcmp(arg, "--type") == 0 && i + 1 < argc) {
            const char* typeStr = argv[++i];
            if (std::strcmp(typeStr, "normal") == 0) {
                genType = voxelmmo::GeneratorType::NORMAL;
            } else if (std::strcmp(typeStr, "test") == 0) {
                genType = voxelmmo::GeneratorType::TEST;
            } else {
                std::cerr << "Unknown generator type: " << typeStr << "\n";
                printUsage(argv[0]);
                return 1;
            }
        }
        else if (std::strcmp(arg, "--test-entity-type") == 0 && i + 1 < argc) {
            const char* entityStr = argv[++i];
            voxelmmo::EntityType type;
            if (!voxelmmo::stringToEntityType(entityStr, type)) {
                std::cerr << "Unknown test entity type: " << entityStr << "\n";
                printUsage(argv[0]);
                return 1;
            }
            testEntityType = type;
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // ── Initialize GameEngine with configured parameters ───────────────────
    voxelmmo::GameEngine game(seed, seedProvided, genType, testEntityType);
    voxelmmo::GatewayEngine gateway;

    // ── Wire GameEngine → GatewayEngine ────────────────────────────────────
    game.setOutputCallback(
        [&](voxelmmo::GatewayId /*gwId*/, const uint8_t* data, size_t size) {
            gateway.receiveGameBatch(data, size);
        });

    game.setPlayerOutputCallback(
        [&](voxelmmo::PlayerId pid, const uint8_t* data, size_t size) {
            gateway.receivePlayerMessage(pid, data, size);
        });

    // ── Wire GatewayEngine → GameEngine ────────────────────────────────────
    game.registerGateway(0);

    gateway.setPlayerConnectCallback([&](voxelmmo::PlayerId pid) {
        game.registerPlayer(0, pid);
        std::cout << "[main] Player " << pid << " connected (pending JOIN)\n";
        // Entity is spawned when the client sends JOIN; sendSnapshot() is called then.
    });

    gateway.setPlayerDisconnectCallback([&](voxelmmo::PlayerId pid) {
        //game.removePlayer(pid);
    });

    gateway.setPlayerInputCallback([&](voxelmmo::PlayerId pid,
                                       const uint8_t* data, size_t size) {
        // TODO: just stack on a pre-allocated gameEngine.playersInputsBuffer
        // [size][pid][data][size][pid][data]...
        game.handlePlayerInput(pid, data, size);
    });

    // ── Game loop (separate thread) ────────────────────────────────────────
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

    // ── Gateway (blocks main thread with uWS event loop) ───────────────────
    std::cout << "[main] Starting voxelmmo server...\n";
    std::cout << "[main] World type: " << (genType == voxelmmo::GeneratorType::TEST ? "test" : "normal")
              << ", seed: " << game.getWorldGenerator().getSeed();
    if (genType == voxelmmo::GeneratorType::TEST) {
        auto tet = game.getWorldGenerator().getTestEntityType();
        std::cout << ", test entity: " << (tet ? voxelmmo::entityTypeToString(*tet) : "none");
    }
    std::cout << "\n";
    gateway.listen(GATEWAY_PORT); // blocks

    running = false;
    gameThread.join();
    return 0;
}
