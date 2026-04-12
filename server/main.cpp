#include "game/GameEngine.hpp"
#include "gateway/GatewayEngine.hpp"
#include "game/SaveSystem.hpp"
#include "game/WorldGenerator.hpp"
#include "common/EntityType.hpp"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <csignal>

static constexpr int GATEWAY_PORT = 8080;

/** Global pointer to GameEngine for signal handler access */
static voxelmmo::GameEngine* g_gameEngine = nullptr;

/** Global state for signal handler */
static std::atomic<bool> g_shutdownRequested{false};

void onSigint(int) {
    if (!g_shutdownRequested.exchange(true)) {
        std::cout << "\n[shutdown] Saving game..." << std::endl;
        
        // Save all game state from signal handler
        if (g_gameEngine) {
            std::cout << "[shutdown] Saving active chunks..." << std::endl;
            g_gameEngine->saveActiveChunks();
            std::cout << "[shutdown] Saving global state..." << std::endl;
            g_gameEngine->saveGlobalState();
            std::cout << "[shutdown] Save complete" << std::endl;
        }
        
        std::cout << "[shutdown] Goodbye!" << std::endl;
        std::_Exit(0);
    } else {
        std::cout << "\n[shutdown] Force quit!" << std::endl;
        std::_Exit(1);
    }
}

static void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "Options:\n"
              << "  --seed <n>                World generation seed (default: random)\n"
              << "  --type <normal|test>      World generation type (default: normal)\n"
              << "  --test-entity-type <type> Entity type for TEST mode (optional)\n"
              << "                            Available types: PLAYER, GHOST_PLAYER, SHEEP\n"
              << "                            If not specified, no entities spawn (player-only)\n"
              << "  --game-key <key>          Save directory name (default: "
              << voxelmmo::SaveSystem::DEFAULT_GAME_KEY << ")\n"
              << "  --help                    Show this help message\n";
}

int main(int argc, char* argv[]) {
    // ── Setup signal handling ──────────────────────────────────────────────
    std::signal(SIGINT, onSigint);
    std::signal(SIGTERM, onSigint);
    
    // ── Parse CLI arguments ────────────────────────────────────────────────
    uint32_t cliSeed = 0;
    bool seedProvided = false;
    voxelmmo::GeneratorType cliGenType = voxelmmo::GeneratorType::NORMAL;
    std::optional<voxelmmo::EntityType> testEntityType;
    std::string gameKey = voxelmmo::SaveSystem::DEFAULT_GAME_KEY;
    
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        else if (std::strcmp(arg, "--seed") == 0 && i + 1 < argc) {
            cliSeed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
            seedProvided = true;
        }
        else if (std::strcmp(arg, "--type") == 0 && i + 1 < argc) {
            const char* typeStr = argv[++i];
            if (std::strcmp(typeStr, "normal") == 0) {
                cliGenType = voxelmmo::GeneratorType::NORMAL;
            } else if (std::strcmp(typeStr, "test") == 0) {
                cliGenType = voxelmmo::GeneratorType::TEST;
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
        else if (std::strcmp(arg, "--game-key") == 0 && i + 1 < argc) {
            gameKey = argv[++i];
        }
        else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }
    
    // ── Initialize GameEngine (creates SaveSystem, loads global state, configures WorldGenerator) ──
    voxelmmo::GameEngine game(cliSeed, cliGenType, seedProvided, testEntityType, gameKey);
    
    voxelmmo::GatewayEngine gateway;

    // Set up global pointer for signal handler
    g_gameEngine = &game;

    // ── Wire GameEngine → GatewayEngine ────────────────────────────────────
    game.setOutputCallback(
        [&](voxelmmo::GatewayId /*gwId*/, const uint8_t* data, size_t size) {
            gateway.receiveGameMessage(data, size);
        });

    game.setPlayerOutputCallback(
        [&](voxelmmo::PlayerId pid, const uint8_t* data, size_t size) {
            gateway.receiveGameMessageForPlayer(pid, data, size);
        });

    // ── Wire GatewayEngine → GameEngine ────────────────────────────────────
    game.registerGateway(0);

    gateway.setPlayerConnectCallback([&](voxelmmo::PlayerId pid) {
        game.registerPlayer(0, pid);
    });

    gateway.setPlayerDisconnectCallback([&](voxelmmo::PlayerId pid) {
        game.markPlayerDisconnected(pid);
    });

    gateway.setPlayerInputCallback([&](voxelmmo::PlayerId pid,
                                       const uint8_t* data, size_t size) {
        game.handlePlayerInput(pid, data, size);
    });

    // ── Start services ─────────────────────────────────────────────────────
    std::cout << "[main] Starting voxelmmo server...\n";
    std::cout << "[main] World type: " << (game.getGeneratorType() == voxelmmo::GeneratorType::TEST ? "test" : "normal")
              << ", seed: " << game.getSeed();
    if (game.getGeneratorType() == voxelmmo::GeneratorType::TEST) {
        auto tet = game.getWorldGenerator().getTestEntityType();
        std::cout << ", test entity: " << (tet ? voxelmmo::entityTypeToString(*tet) : "none");
    }
    std::cout << "\n";
    std::cout << "[main] Save directory: " << game.getSaveDirectory() << "\n";
    std::cout << "[main] Press Ctrl+C to save and quit (or twice to force quit)\n";

    // Start game loop in a separate thread
    std::thread gameThread([&]() {
        game.run();
    });

    // Run gateway in main thread (blocks until process ends)
    gateway.listen(GATEWAY_PORT);

    // This point is only reached if gateway.listen() returns (which shouldn't happen)
    std::cout << "[main] Gateway stopped unexpectedly\n";
    
    // Cleanup (shouldn't reach here in normal operation)
    if (gameThread.joinable()) {
        game.stop();
        gameThread.join();
    }
    
    return 0;
}
