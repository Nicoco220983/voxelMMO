// @ts-check
import { VoxelType } from '../VoxelTypes.js'

/**
 * Face indices in priority order for ladder attachment.
 * Order: -X (1), +X (0), -Z (5), +Z (4).
 * Ladder renders ON this face (adjacent to solid neighbor).
 * @type {number[]}
 */
export const LADDER_FACE_PRIORITY = [1, 0, 5, 4]

/**
 * Render a ladder voxel with special graphics.
 * Attached to solid neighbors, or free-standing in X orientation.
 * @param {import('./index.js').RenderContext} ctx
 */
export function renderLadderCustom(ctx) {
  const {
    x, y, z, vtype, jitter,
    positions, colors, uvs, normals, indices,
    isSolid, getVoxelUvs,
    FACE_SHADE, FACE_NORMALS, FACE_VERTS
  } = ctx

  // Check for solid neighbors to attach to
  let selectedFace = -1
  for (const face of LADDER_FACE_PRIORITY) {
    const [dx, dy, dz] = ctx.FACE_DIRS[face]
    if (isSolid(x + dx, y + dy, z + dz)) {
      selectedFace = face
      break
    }
  }

  const { u0, v0, u1, v1 } = getVoxelUvs(vtype, 0)
  const faceUvs = [
    [u0, v1], [u0, v0], [u1, v0], [u1, v1],
  ]

  const shade = (selectedFace !== -1 ? FACE_SHADE[selectedFace] : 0.85) * jitter
  const r = shade, g = shade, b = shade

  if (selectedFace !== -1) {
    // Attached to solid: render single face, inset to avoid z-fighting
    const [nx, ny, nz] = FACE_NORMALS[selectedFace]
    // Inset by 0.05 units (away from solid neighbor, toward voxel center)
    const inset = 0.05
    const ix = -nx * inset
    const iy = -ny * inset
    const iz = -nz * inset

    let base = positions.length / 3
    // Reverse vertex order (3, 2, 1, 0) to flip normal direction (point toward player)
    for (let vi = 3; vi >= 0; vi--) {
      const [fx, fy, fz] = FACE_VERTS[selectedFace][vi]
      positions.push(x + fx + ix, y + fy + iy, z + fz + iz)
      colors.push(r, g, b)
      normals.push(-nx, -ny, -nz)  // Inverted normal
      const [fu, fv] = faceUvs[3 - vi]
      uvs.push(fu, fv)
    }
    indices.push(base, base + 1, base + 2, base, base + 2, base + 3)
  } else {
    // Free-standing: single thin vertical face in X orientation (full voxel width)
    const xPos = x + 0.5  // Center X
    const halfThick = 0.05
    const zMin = z + 0    // Full voxel width in Z
    const zMax = z + 1

    // Face visible from +X side (normal pointing +X)
    let base = positions.length / 3
    positions.push(xPos + halfThick, y + 0, zMax)
    positions.push(xPos + halfThick, y + 1, zMax)
    positions.push(xPos + halfThick, y + 1, zMin)
    positions.push(xPos + halfThick, y + 0, zMin)
    for (let i = 0; i < 4; i++) {
      colors.push(r, g, b)
      normals.push(1, 0, 0)
      const [fu, fv] = faceUvs[i]
      uvs.push(fu, fv)
    }
    indices.push(base, base + 1, base + 2, base, base + 2, base + 3)

    // Face visible from -X side (normal pointing -X)
    base = positions.length / 3
    positions.push(xPos - halfThick, y + 0, zMin)
    positions.push(xPos - halfThick, y + 1, zMin)
    positions.push(xPos - halfThick, y + 1, zMax)
    positions.push(xPos - halfThick, y + 0, zMax)
    for (let i = 0; i < 4; i++) {
      colors.push(r, g, b)
      normals.push(-1, 0, 0)
      const [fu, fv] = faceUvs[i]
      uvs.push(fu, fv)
    }
    indices.push(base, base + 1, base + 2, base, base + 2, base + 3)
  }
}

/** @type {import('./index.js').VoxelDef} */
export const LadderVoxel = {
  type: VoxelType.LADDER,
  name: 'ladder',
  textures: 'ladder',
  isSolid: false,
  isOpaque: false,
  renderCustom: renderLadderCustom,
}
