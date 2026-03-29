// @ts-check
import { VoxelType } from '../types.js'

/** @type {import('./index.js').VoxelDef} */
export const GrassVoxel = {
  type: VoxelType.GRASS,
  name: 'grass',
  textures: {
    default: 'dirt',
    2: 'grass', // +Y top face
  },
}
