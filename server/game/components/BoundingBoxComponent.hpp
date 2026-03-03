#pragma once
#include <cstdint>

namespace voxelmmo {

/**
 * @brief Axis-aligned bounding box half-extents for an entity (sub-voxels).
 *
 * The entity's AABB is centred on its DynamicPositionComponent position:
 *   minX = x - hx,  maxX = x + hx
 *   minY = y - hy,  maxY = y + hy
 *   minZ = z - hz,  maxZ = z + hz
 */
struct BoundingBoxComponent {
    int32_t hx{0};  ///< Half-extent in X (sub-voxels).
    int32_t hy{0};  ///< Half-extent in Y (sub-voxels).
    int32_t hz{0};  ///< Half-extent in Z (sub-voxels).
};

} // namespace voxelmmo
