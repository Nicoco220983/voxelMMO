#pragma once
#include "common/Types.hpp"
#include <set>
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Utility for calculating which chunks are watched by a player.
 *
 * Extracted to common/ so it can be used by both GameEngine (for chunk
 * activation) and GatewayEngine (for late-joiner sync).
 */
struct WatchedChunksTracker {
    /**
     * @brief Calculate the set of chunks watched by a player at position.
     *
     * @param sx, sy, sz    Player position in sub-voxels
     * @param watchRadius   Horizontal radius (in chunks) to watch
     * @param verticalRange Vertical range (in chunks) above/below player
     * @return Set of ChunkIds the player should receive updates for
     */
    static std::set<ChunkId> calculateWatchedChunks(
        int32_t sx,
        int32_t sy,
        int32_t sz,
        int32_t watchRadius,
        int32_t verticalRange = 1
    ) {
        std::set<ChunkId> result;
        
        const int32_t cx = sx >> CHUNK_SHIFT_X;
        const int32_t cy = sy >> CHUNK_SHIFT_Y;
        const int32_t cz = sz >> CHUNK_SHIFT_Z;
        
        for (int32_t dx = -watchRadius; dx <= watchRadius; ++dx) {
            for (int32_t dy = -verticalRange; dy <= verticalRange; ++dy) {
                for (int32_t dz = -watchRadius; dz <= watchRadius; ++dz) {
                    const ChunkId cid = ChunkId::make(cy + dy, cx + dx, cz + dz);
                    result.insert(cid);
                }
            }
        }
        
        return result;
    }
};

} // namespace voxelmmo
