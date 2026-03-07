#include "gateway/GatewayEngine.hpp"
#include "common/MessageTypes.hpp"
#include <iostream>
#include <cstring>
#include <vector>

namespace voxelmmo {

GatewayEngine::GatewayEngine()  = default;
GatewayEngine::~GatewayEngine() = default;

void GatewayEngine::setPlayerConnectCallback(PlayerConnectCallback cb)       { connectCb    = std::move(cb); }
void GatewayEngine::setPlayerDisconnectCallback(PlayerDisconnectCallback cb)  { disconnectCb = std::move(cb); }
void GatewayEngine::setPlayerInputCallback(PlayerInputCallback cb)             { inputCb      = std::move(cb); }

// ── Incoming from game engine ─────────────────────────────────────────────

void GatewayEngine::receiveGameBatch(const uint8_t* data, size_t size) {
    // Copy the batch so it can be safely captured across the thread boundary
    auto buf = std::make_shared<std::vector<uint8_t>>(data, data + size);

    uwsLoop->defer([this, buf]() {
        // Parse individual messages using their embedded [type(1)][size(2)] header
        // and update StateManager for each message.
        size_t off = 0;
        while (off + 3 <= buf->size()) {
            // Read size from header (bytes 1-2, little-endian)
            uint16_t msgSize;
            std::memcpy(&msgSize, buf->data() + off + 1, 2);
            if (off + msgSize > buf->size()) break;
            stateManager.receiveChunkMessage(buf->data() + off, msgSize);
            off += msgSize;
        }
        // Forward the entire batch as a single WebSocket frame to all clients
        // TODO: filter per-client by watched chunks
        broadcastBatch(buf->data(), buf->size());
    });
}

void GatewayEngine::broadcastBatch(const uint8_t* data, size_t size) {
    const std::string_view msg(reinterpret_cast<const char*>(data), size);
    for (auto& [pid, ws] : sockets) {
        ws->send(msg, uWS::OpCode::BINARY);
    }
}

// ── WebSocket server ──────────────────────────────────────────────────────

void GatewayEngine::listen(int port) {
    // Capture the loop pointer now, while we are on the uWS thread.
    // receiveGameBatch() will use this from the game thread.
    uwsLoop = uWS::Loop::get();

    wsApp.ws<PlayerConnection>("/*", {
        /* Settings */
        .maxPayloadLength = 16 * 1024,
        .idleTimeout      = 120,

        .open = [this](uWS::WebSocket<false, true, PlayerConnection>* ws) {
            const PlayerId pid = nextPlayerId++;
            ws->getUserData()->playerId = pid;
            sockets[pid] = ws;
            std::cout << "[gateway] Player " << pid << " connected\n";
            if (connectCb) connectCb(pid);
        },

        .message = [this](uWS::WebSocket<false, true, PlayerConnection>* ws,
                          std::string_view msg, uWS::OpCode opCode) {
            if (opCode != uWS::OpCode::BINARY) return;
            const PlayerId pid = ws->getUserData()->playerId;
            if (inputCb) {
                inputCb(pid,
                        reinterpret_cast<const uint8_t*>(msg.data()),
                        msg.size());
            }
        },

        .close = [this](uWS::WebSocket<false, true, PlayerConnection>* ws,
                        int /*code*/, std::string_view /*reason*/) {
            const PlayerId pid = ws->getUserData()->playerId;
            sockets.erase(pid);
            stateManager.removePlayer(pid);
            std::cout << "[gateway] Player " << pid << " disconnected\n";
            if (disconnectCb) disconnectCb(pid);
        },
    })
    .listen(port, [port](us_listen_socket_t* listenSocket) {
        if (listenSocket) {
            std::cout << "[gateway] Listening on port " << port << "\n";
        } else {
            std::cerr << "[gateway] Failed to bind port " << port << "\n";
        }
    })
    .run();
}

} // namespace voxelmmo
