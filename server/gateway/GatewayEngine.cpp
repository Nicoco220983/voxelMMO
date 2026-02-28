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

void GatewayEngine::receiveGameMessage(const uint8_t* data, size_t size) {
    // Copy into a shared_ptr so it can be safely captured across thread boundary
    auto buf = std::make_shared<std::vector<uint8_t>>(data, data + size);

    // Defer execution onto the uWS event loop (uwsLoop captured on the uWS thread)
    uwsLoop->defer([this, buf]() {
        stateManager.receiveChunkMessage(buf->data(), buf->size());

        if (buf->size() < 1 + sizeof(int64_t)) return;
        ChunkId cid;
        std::memcpy(&cid.packed, buf->data() + 1, sizeof(int64_t));
        broadcastChunkMessage(cid, buf->data(), buf->size());
    });
}

void GatewayEngine::broadcastChunkMessage(ChunkId /*cid*/, const uint8_t* data, size_t size) {
    const std::string_view msg(reinterpret_cast<const char*>(data), size);
    // TODO: only send to players watching cid (requires per-player watched-chunk tracking)
    for (auto& [pid, ws] : sockets) {
        ws->send(msg, uWS::OpCode::BINARY);
    }
}

// ── WebSocket server ──────────────────────────────────────────────────────

void GatewayEngine::listen(int port) {
    // Capture the loop pointer now, while we are on the uWS thread.
    // receiveGameMessage() will use this from the game thread.
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
