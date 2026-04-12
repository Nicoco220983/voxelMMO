#include "gateway/GatewayEngine.hpp"
#include "gateway/ChunkState.hpp"
#include "gateway/PlayerInfo.hpp"
#include "common/NetworkProtocol.hpp"
#include <iostream>
#include <cstring>
#include <vector>
#include <unordered_map>

namespace voxelmmo {

// Implementation class
class GatewayEngine::Impl {
public:
    uWS::App wsApp;
    uWS::Loop* uwsLoop{nullptr};
    us_listen_socket_t* listenSocket_{nullptr};

    std::unordered_map<PlayerId, uWS::WebSocket<false, true, PlayerConnection>*> sockets;

    GatewayEngine::PlayerConnectCallback connectCb;
    GatewayEngine::PlayerDisconnectCallback disconnectCb;
    GatewayEngine::PlayerInputCallback inputCb;

    std::unordered_map<ChunkId, ChunkState> chunkStates;
    std::unordered_map<PlayerId, PlayerInfo> players;
};

GatewayEngine::GatewayEngine() 
    : pImpl(std::make_unique<Impl>()) {}

GatewayEngine::~GatewayEngine() = default;

void GatewayEngine::setPlayerConnectCallback(PlayerConnectCallback cb) { 
    pImpl->connectCb = std::move(cb); 
}

void GatewayEngine::setPlayerDisconnectCallback(PlayerDisconnectCallback cb) { 
    pImpl->disconnectCb = std::move(cb); 
}

void GatewayEngine::setPlayerInputCallback(PlayerInputCallback cb) { 
    pImpl->inputCb = std::move(cb); 
}

// ── Incoming from game engine ─────────────────────────────────────────────

void GatewayEngine::receiveGameMessage(const uint8_t* data, size_t size) {
    if (!pImpl->uwsLoop) return;
    
    // Copy the batch so it can be safely captured across the thread boundary
    auto buf = std::make_shared<std::vector<uint8_t>>(data, data + size);

    pImpl->uwsLoop->defer([this, buf]() {
        // Parse individual messages using their embedded [type(1)][size(2)] header
        // and update chunk state for each message.
        size_t off = 0;
        while (off + 3 <= buf->size()) {
            // Read size from header (bytes 1-2, little-endian)
            uint16_t msgSize;
            std::memcpy(&msgSize, buf->data() + off + 1, 2);
            if (off + msgSize > buf->size()) break;
            receiveChunkMessage(buf->data() + off, msgSize);
            off += msgSize;
        }
        // Forward the entire batch as a single WebSocket frame to all clients
        broadcastBatch(buf->data(), buf->size());
    });
}

void GatewayEngine::receiveGameMessageForPlayer(PlayerId playerId, const uint8_t* data, size_t size) {
    if (!pImpl->uwsLoop) return;
    
    // Copy the message so it can be safely captured across the thread boundary
    auto buf = std::make_shared<std::vector<uint8_t>>(data, data + size);

    pImpl->uwsLoop->defer([this, playerId, buf]() {
        sendToPlayer(playerId, buf->data(), buf->size());
    });
}

void GatewayEngine::broadcastBatch(const uint8_t* data, size_t size) {
    const std::string_view msg(reinterpret_cast<const char*>(data), size);
    for (auto& [pid, ws] : pImpl->sockets) {
        ws->send(msg, uWS::OpCode::BINARY);
    }
}

bool GatewayEngine::sendToPlayer(PlayerId playerId, const uint8_t* data, size_t size) {
    auto it = pImpl->sockets.find(playerId);
    if (it == pImpl->sockets.end()) return false;
    const std::string_view msg(reinterpret_cast<const char*>(data), size);
    it->second->send(msg, uWS::OpCode::BINARY);
    return true;
}

// ── WebSocket server ──────────────────────────────────────────────────────

void GatewayEngine::stop() {
    if (pImpl->uwsLoop) {
        pImpl->uwsLoop->defer([this]() {
            std::cout << "[gateway] Stopping server...\n";
            
            // Close all WebSocket connections gracefully
            for (auto& [pid, ws] : pImpl->sockets) {
                ws->close();
            }
            pImpl->sockets.clear();
            
            // Close listen socket to stop accepting new connections
            if (pImpl->listenSocket_) {
                us_listen_socket_close(0, pImpl->listenSocket_);
                pImpl->listenSocket_ = nullptr;
                std::cout << "[gateway] Listen socket closed\n";
            }
        });
    }
}

// ── Join handling ──────────────────────────────────────────────────────────

void GatewayEngine::handleJoin(uWS::WebSocket<false, true, PlayerConnection>* ws,
                                const NetworkProtocol::JoinMessage& joinMsg) {
    // Derive PlayerId deterministically from session token
    const PlayerId pid = NetworkProtocol::playerIdFromSessionToken(joinMsg.sessionToken);
    
    // Check if this PlayerId is already connected (reconnection)
    auto existingIt = pImpl->sockets.find(pid);
    if (existingIt != pImpl->sockets.end()) {
        // Only close if it's a different socket (actual reconnection)
        // Same socket = respawn, not reconnection
        if (existingIt->second != ws) {
            // Close the old connection
            existingIt->second->close();
            pImpl->sockets.erase(existingIt);
            removePlayer(pid);
            if (pImpl->disconnectCb) pImpl->disconnectCb(pid);
        }
    }
    
    // Assign PlayerId to this connection
    ws->getUserData()->playerId = pid;
    pImpl->sockets[pid] = ws;
    
    // Send all cached chunk state to the new player
    for (const auto& [cid, state] : pImpl->chunkStates) {
        auto [stateData, length] = state.getDataToSend(0);
        if (length > 0) {
            sendToPlayer(pid, stateData, length);
        }
    }
    
    if (pImpl->connectCb) pImpl->connectCb(pid);
}

void GatewayEngine::listen(int port) {
    // Capture the loop pointer now, while we are on the uWS thread.
    // receiveGameMessage() will use this from the game thread.
    pImpl->uwsLoop = uWS::Loop::get();

    pImpl->wsApp.template ws<PlayerConnection>("/*", {
        .maxPayloadLength = 16 * 1024,
        .idleTimeout      = 120,

        .open = [this](uWS::WebSocket<false, true, PlayerConnection>* ws) {
            // PlayerId not yet assigned - will be derived from session token on JOIN
            ws->getUserData()->playerId = 0;
        },

        .message = [this](uWS::WebSocket<false, true, PlayerConnection>* ws,
                          std::string_view msg, uWS::OpCode opCode) {
            if (opCode != uWS::OpCode::BINARY) return;
            
            const uint8_t* data = reinterpret_cast<const uint8_t*>(msg.data());
            const size_t size = msg.size();
            
            // Check if this is a JOIN message (first byte = ClientMessageType::JOIN = 1)
            if (size >= 1 && data[0] == static_cast<uint8_t>(ClientMessageType::JOIN)) {
                auto joinMsg = NetworkProtocol::parseJoin(data, size);
                if (!joinMsg) return;  // Invalid JOIN message
                handleJoin(ws, *joinMsg);
            }
            
            // Forward all messages (including JOIN) to the game engine
            const PlayerId pid = ws->getUserData()->playerId;
            if (pid != 0 && pImpl->inputCb) {
                pImpl->inputCb(pid, data, size);
            }
        },

        .close = [this](uWS::WebSocket<false, true, PlayerConnection>* ws,
                        int /*code*/, std::string_view /*reason*/) {
            const PlayerId pid = ws->getUserData()->playerId;
            if (pid != 0) {
                pImpl->sockets.erase(pid);
                removePlayer(pid);
                if (pImpl->disconnectCb) pImpl->disconnectCb(pid);
            }
        },
    })
    .listen(port, [this, port](us_listen_socket_t* listenSocket) {
        if (listenSocket) {
            pImpl->listenSocket_ = listenSocket;
            std::cout << "[gateway] Listening on port " << port << "\n";
        } else {
            std::cerr << "[gateway] Failed to bind port " << port << "\n";
        }
    })
    .run();
}

// ── Per-chunk state management ────────────────────────────────────────────

void GatewayEngine::receiveChunkMessage(const uint8_t* data, size_t size) {
    // Minimum header: [type(1)][size(2)][chunk_id(8)][tick(4)] = 15 bytes
    if (size < NetworkProtocol::CHUNK_MESSAGE_HEADER_SIZE) return;

    // size is at bytes 1-2, chunk_id at bytes 3-10
    ChunkId cid;
    std::memcpy(&cid.packed, data + 3, sizeof(int64_t));

    ChunkState& state = getChunkState(cid);
    
    // receiveMessage handles all message types appropriately:
    // - Snapshots: clear all existing data
    // - Snapshot deltas: clear previous deltas (keep snapshot), append delta
    // - Tick deltas: just append
    state.receiveMessage(data, size);
}

ChunkState& GatewayEngine::getChunkState(ChunkId id) {
    return pImpl->chunkStates[id];
}

const ChunkState* GatewayEngine::findChunkState(ChunkId id) const {
    const auto it = pImpl->chunkStates.find(id);
    return (it != pImpl->chunkStates.end()) ? &it->second : nullptr;
}

void GatewayEngine::removeChunk(ChunkId id) {
    pImpl->chunkStates.erase(id);
}

// ── Per-player snapshot tick tracking ─────────────────────────────────────

void GatewayEngine::setPlayerStateTick(PlayerId pid, ChunkId cid, uint32_t tick) {
    pImpl->players[pid].lastStateTick[cid] = tick;
}

uint32_t GatewayEngine::getPlayerStateTick(PlayerId pid, ChunkId cid) const {
    const auto pit = pImpl->players.find(pid);
    if (pit == pImpl->players.end()) return 0;
    const auto cit = pit->second.lastStateTick.find(cid);
    return (cit != pit->second.lastStateTick.end()) ? cit->second : 0;
}

void GatewayEngine::removePlayer(PlayerId pid) {
    pImpl->players.erase(pid);
}

} // namespace voxelmmo
