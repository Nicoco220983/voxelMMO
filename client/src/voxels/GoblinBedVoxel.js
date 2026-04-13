// @ts-check
import { VoxelType } from '../VoxelTypes.js'
import { createVoxel } from './BaseVoxel.js'

/**
 * Render a simple low-height bed - just 6 faces of a flat rectangle.
 * Bed occupies bottom 25% of the voxel space.
 *
 * @param {import('./index.js').RenderContext} ctx
 */
function renderBed(ctx) {
  const { x, y, z, jitter, positions, colors, uvs, normals, indices, getVoxelUvs } = ctx
  const { u0, v0, u1, v1 } = getVoxelUvs(VoxelType.GOBLIN_BED, 0)

  const yStart = y
  const yEnd = y + 0.25  // 25% height like a thin bed
  const shade = [0.85, 0.95, 1.0, 0.65, 0.8, 0.75]  // -X, +X, +Y, -Y, +Z, -Z face shades

  // Helper to add a face (vertices in CCW order when looking at the face from outside = against normal direction)
  const addFace = (verts, normalIdx) => {
    const base = positions.length / 3
    const s = shade[normalIdx] * jitter
    for (let i = 0; i < 12; i += 3) {
      positions.push(verts[i], verts[i + 1], verts[i + 2])
      colors.push(s, s, s)
    }
    // Normal
    const nx = normalIdx === 0 ? -1 : normalIdx === 1 ? 1 : 0
    const ny = normalIdx === 2 ? 1 : normalIdx === 3 ? -1 : 0
    const nz = normalIdx === 4 ? 1 : normalIdx === 5 ? -1 : 0
    for (let i = 0; i < 4; i++) normals.push(nx, ny, nz)
    // UVs
    uvs.push(u0, v1, u0, v0, u1, v0, u1, v1)
    // Indices (CCW winding)
    indices.push(base, base + 1, base + 2, base, base + 2, base + 3)
  }

  const x1 = x + 1, z1 = z + 1

  // -X face (normal points -X): visible when looking from -X (left)
  // CCW from left view: (x,y,z1) -> (x,y1,z1) -> (x,y1,z) -> (x,y,z)
  addFace([x, yStart, z1, x, yEnd, z1, x, yEnd, z, x, yStart, z], 0)
  // +X face (normal points +X): visible when looking from +X (right)  
  // CCW from right view: (x1,y,z) -> (x1,y1,z) -> (x1,y1,z1) -> (x1,y,z1)
  addFace([x1, yStart, z, x1, yEnd, z, x1, yEnd, z1, x1, yStart, z1], 1)
  // +Y face/top (normal points +Y): visible when looking from above
  // CCW from above: (x,y1,z1) -> (x1,y1,z1) -> (x1,y1,z) -> (x,y1,z)
  addFace([x, yEnd, z1, x1, yEnd, z1, x1, yEnd, z, x, yEnd, z], 2)
  // -Y face/bottom (normal points -Y): visible when looking from below
  // CCW from below: (x,y,z) -> (x1,y,z) -> (x1,y,z1) -> (x,y,z1)
  addFace([x, yStart, z, x1, yStart, z, x1, yStart, z1, x, yStart, z1], 3)
  // +Z face (normal points +Z): visible when looking from +Z (front)
  // CCW from front view: (x1,y,z1) -> (x1,y1,z1) -> (x,y1,z1) -> (x,y,z1)
  addFace([x1, yStart, z1, x1, yEnd, z1, x, yEnd, z1, x, yStart, z1], 4)
  // -Z face (normal points -Z): visible when looking from -Z (back)
  // CCW from back view: (x,y,z) -> (x,y1,z) -> (x1,y1,z) -> (x1,y,z)
  addFace([x, yStart, z, x, yEnd, z, x1, yEnd, z, x1, yStart, z], 5)
}

/**
 * Goblin Bed voxel - a simple low-height 3D rectangle (bed/mat on the floor).
 */
export const GoblinBedVoxel = createVoxel({
  type: VoxelType.GOBLIN_BED,
  name: 'goblin_bed',
  textures: 'goblin_bed',
  isSolid: false,
  isOpaque: false,
  renderCustom: renderBed,
})
