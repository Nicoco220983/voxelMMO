#include "game/WorldGenerator.hpp"
#include "game/entities/SheepEntity.hpp"
#include "game/components/GlobalEntityIdComponent.hpp"
#include "game/components/DirtyComponent.hpp"
#include "game/systems/EntityStateSystem.hpp"
#include "common/Types.hpp"
#include <cmath>
#include <algorithm>

namespace {

// ── Permutation table (Perlin's classic 256-entry sequence, doubled) ─────────

static const uint8_t PERM[512] = {
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180,
    // repeated
    151,160,137, 91, 90, 15,131, 13,201, 95, 96, 53,194,233,  7,225,
    140, 36,103, 30, 69,142,  8, 99, 37,240, 21, 10, 23,190,  6,148,
    247,120,234, 75,  0, 26,197, 62, 94,252,219,203,117, 35, 11, 32,
     57,177, 33, 88,237,149, 56, 87,174, 20,125,136,171,168, 68,175,
     74,165, 71,134,139, 48, 27,166, 77,146,158,231, 83,111,229,122,
     60,211,133,230,220,105, 92, 41, 55, 46,245, 40,244,102,143, 54,
     65, 25, 63,161,  1,216, 80, 73,209, 76,132,187,208, 89, 18,169,
    200,196,135,130,116,188,159, 86,164,100,109,198,173,186,  3, 64,
     52,217,226,250,124,123,  5,202, 38,147,118,126,255, 82, 85,212,
    207,206, 59,227, 47, 16, 58, 17,182,189, 28, 42,223,183,170,213,
    119,248,152,  2, 44,154,163, 70,221,153,101,155,167, 43,172,  9,
    129, 22, 39,253, 19, 98,108,110, 79,113,224,232,178,185,112,104,
    218,246, 97,228,251, 34,242,193,238,210,144, 12,191,179,162,241,
     81, 51,145,235,249, 14,239,107, 49,192,214, 31,181,199,106,157,
    184, 84,204,176,115,121, 50, 45,127,  4,150,254,138,236,205, 93,
    222,114, 67, 29, 24, 72,243,141,128,195, 78, 66,215, 61,156,180,
};

// ── 8 unit-vector gradients for 2D ────────────────────────────────────────────
static const int8_t GRAD2[8][2] = {
    { 1, 1}, {-1, 1}, { 1,-1}, {-1,-1},
    { 1, 0}, {-1, 0}, { 0, 1}, { 0,-1},
};

// Skew / unskew factors for 2D simplex (Gustavson 2005)
static constexpr float F2 = 0.3660254037844386f;   // (sqrt(3) - 1) / 2
static constexpr float G2 = 0.21132486540518713f;  // (3 - sqrt(3)) / 6

// ── 2D simplex noise → approximately [-1, 1] ──────────────────────────────────
static float simplex2D(float x, float y) noexcept {
    // Skew input to find which simplex cell we're in
    const float s = (x + y) * F2;
    const int   i = static_cast<int>(std::floor(x + s));
    const int   j = static_cast<int>(std::floor(y + s));

    // Unskew back; distances from corner 0
    const float t  = static_cast<float>(i + j) * G2;
    const float x0 = x - (static_cast<float>(i) - t);
    const float y0 = y - (static_cast<float>(j) - t);

    // Corner ordering within the simplex (two triangles in 2D)
    const int i1 = (x0 > y0) ? 1 : 0;
    const int j1 = (x0 > y0) ? 0 : 1;

    const float x1 = x0 - static_cast<float>(i1) + G2;
    const float y1 = y0 - static_cast<float>(j1) + G2;
    const float x2 = x0 - 1.0f + 2.0f * G2;
    const float y2 = y0 - 1.0f + 2.0f * G2;

    // Hash the three corners
    const int ii = i & 255;
    const int jj = j & 255;
    const int8_t* g0 = GRAD2[PERM[ii      + PERM[jj     ]] & 7];
    const int8_t* g1 = GRAD2[PERM[ii + i1 + PERM[jj + j1]] & 7];
    const int8_t* g2 = GRAD2[PERM[ii + 1  + PERM[jj + 1 ]] & 7];

    // Radial basis contributions from each corner
    auto contrib = [](const int8_t* g, float dx, float dy) noexcept -> float {
        float t = 0.5f - dx*dx - dy*dy;
        if (t < 0.0f) return 0.0f;
        t *= t;
        return t * t * (static_cast<float>(g[0]) * dx + static_cast<float>(g[1]) * dy);
    };

    return 70.0f * (contrib(g0, x0, y0) + contrib(g1, x1, y1) + contrib(g2, x2, y2));
}

// ── Height function ────────────────────────────────────────────────────────────
//
// Returns a world-Y surface height in [4, 30]:
//   - fBm base (3 octaves): rolling hills and valleys,  range ≈ [-7, +7]
//   - Ridged mountain layer (cubed):  sparse tall peaks, range  ∈ [0, 18]
//   - Base offset 5 + clamp to [4, 30]
//
// Guarantees:
//   chunkY = -1  (worldY -16 .. -1)  → always STONE  (surface ≥ 4 > -1)
//   chunkY =  2  (worldY 32  .. 47)  → always AIR    (surface ≤ 30 < 32)
//
static float computeHeight(float wx, float wz) noexcept {
    // fBm: 3 octaves — large hills, medium bumps, fine detail
    const float base = simplex2D(wx / 128.0f, wz / 128.0f) * 4.0f
                     + simplex2D(wx /  64.0f, wz /  64.0f) * 2.0f
                     + simplex2D(wx /  32.0f, wz /  32.0f) * 1.0f;

    // Ridged noise: cubed to concentrate height gain at rare ridge peaks
    const float r     = simplex2D(wx / 256.0f, wz / 256.0f);
    const float ridge = 1.0f - std::abs(r);
    const float mountain = ridge * ridge * ridge * 18.0f;

    return std::clamp(5.0f + base + mountain, 4.0f, 30.0f);
}

} // anonymous namespace

namespace voxelmmo {

int32_t WorldGenerator::surfaceY(float wx, float wz) const noexcept {
    return static_cast<int32_t>(computeHeight(wx, wz));
}

void WorldGenerator::generateEntities(ChunkId chunkId, entt::registry& registry, uint32_t tick) const {
    // Deterministic sheep spawn: based on chunk coordinates
    // Use a simple hash of chunk coordinates to decide if/where sheep spawn
    const int32_t cx = chunkId.x();
    const int32_t cy = chunkId.y();
    const int32_t cz = chunkId.z();
    
    // Only spawn sheep in surface chunks (cy where grass exists: typically 0 or 1)
    if (cy < 0 || cy > 1) return;
    
    // Hash chunk coords for deterministic spawning
    const uint32_t hash = static_cast<uint32_t>(cx * 73856093 ^ cz * 19349663);
    
    // 30% chance to spawn sheep in eligible chunks
    if ((hash % 100) >= 30) return;
    
    // Spawn 1-3 sheep per chunk
    const int sheepCount = 1 + (hash % 3);
    
    for (int i = 0; i < sheepCount; ++i) {
        // Deterministic position within chunk
        const uint32_t posHash = hash + i * 1234567;
        const int32_t localX = static_cast<int32_t>(posHash % CHUNK_SIZE_X);
        const int32_t localZ = static_cast<int32_t>((posHash / CHUNK_SIZE_X) % CHUNK_SIZE_Z);
        
        // Find surface height at this position
        const float wx = static_cast<float>(cx * CHUNK_SIZE_X + localX);
        const float wz = static_cast<float>(cz * CHUNK_SIZE_Z + localZ);
        const int32_t surface = surfaceY(wx, wz);
        
        // Only spawn if surface is in this chunk's Y range
        const int32_t worldY = surface + 1;  // Spawn one block above grass
        if (worldY < cy * CHUNK_SIZE_Y || worldY >= (cy + 1) * CHUNK_SIZE_Y) continue;
        
        // Convert to sub-voxel coordinates
        const int32_t sx = (cx * CHUNK_SIZE_X + localX) << SUBVOXEL_BITS;
        const int32_t sy = worldY << SUBVOXEL_BITS;
        const int32_t sz = (cz * CHUNK_SIZE_Z + localZ) << SUBVOXEL_BITS;
        
        // Create sheep entity and mark for creation
        const entt::entity ent = registry.create();
        registry.emplace<GlobalEntityIdComponent>(ent, static_cast<GlobalEntityId>(tick + i + hash));
        registry.emplace<DirtyComponent>(ent);  // Ensure DirtyComponent exists
        SheepEntity::spawn(registry, ent, sx, sy, sz, chunkId, tick + i);
        
        // Mark for creation - EntityStateSystem will add to chunk
        EntityStateSystem::markForCreation(registry, ent, chunkId);
    }
}

void WorldGenerator::generate(std::vector<VoxelType>& voxels,
                               int32_t chunkX, int32_t chunkY, int32_t chunkZ) const
{
    // ── Step 1: sample height on a coarse (STEP-voxel) grid ──────────────────
    // CHUNK_SIZE_X = CHUNK_SIZE_Z = 64, STEP = 4  →  17 × 17 = 289 evaluations
    // instead of 64 × 64 = 4 096  (~14× fewer noise calls).
    constexpr int STEP   = 4;
    constexpr int GRID_X = CHUNK_SIZE_X / STEP + 1;  // 17
    constexpr int GRID_Z = CHUNK_SIZE_Z / STEP + 1;  // 17

    float heightGrid[GRID_X][GRID_Z];
    for (int gx = 0; gx < GRID_X; ++gx) {
        for (int gz = 0; gz < GRID_Z; ++gz) {
            const float wx = static_cast<float>(chunkX * CHUNK_SIZE_X + gx * STEP);
            const float wz = static_cast<float>(chunkZ * CHUNK_SIZE_Z + gz * STEP);
            heightGrid[gx][gz] = computeHeight(wx, wz);
        }
    }

    // ── Step 2: fill voxels via bilinear interpolation over 4×4 cells ────────
    constexpr int CELLS = CHUNK_SIZE_X / STEP;  // 16  (same in X and Z)

    for (int cell_x = 0; cell_x < CELLS; ++cell_x) {
        for (int cell_z = 0; cell_z < CELLS; ++cell_z) {
            const float h00 = heightGrid[cell_x    ][cell_z    ];
            const float h10 = heightGrid[cell_x + 1][cell_z    ];
            const float h01 = heightGrid[cell_x    ][cell_z + 1];
            const float h11 = heightGrid[cell_x + 1][cell_z + 1];

            for (int lx = 0; lx < STEP; ++lx) {
                const float tx = static_cast<float>(lx) / static_cast<float>(STEP);
                const float hx0 = h00 + (h10 - h00) * tx;
                const float hx1 = h01 + (h11 - h01) * tx;

                for (int lz = 0; lz < STEP; ++lz) {
                    const float tz       = static_cast<float>(lz) / static_cast<float>(STEP);
                    const int32_t surfaceY = static_cast<int32_t>(hx0 + (hx1 - hx0) * tz);

                    const int x = cell_x * STEP + lx;
                    const int z = cell_z * STEP + lz;

                    for (int y = 0; y < CHUNK_SIZE_Y; ++y) {
                        const int32_t worldY = chunkY * CHUNK_SIZE_Y + y;

                        VoxelType type;
                        if      (worldY > surfaceY)        type = VoxelTypes::AIR;
                        else if (worldY == surfaceY)       type = VoxelTypes::GRASS;
                        else if (worldY >= surfaceY - 3)   type = VoxelTypes::DIRT;
                        else                               type = VoxelTypes::STONE;

                        voxels[static_cast<size_t>(y) * CHUNK_SIZE_X * CHUNK_SIZE_Z
                             + static_cast<size_t>(x) * CHUNK_SIZE_Z
                             + static_cast<size_t>(z)] = type;
                    }
                }
            }
        }
    }
}

} // namespace voxelmmo
