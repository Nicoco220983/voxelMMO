// @ts-check
import { VoxelType } from '../VoxelTypes.js'
import { createVoxel } from './BaseVoxel.js'

export const DirtVoxel = createVoxel({
  type: VoxelType.DIRT,
  name: 'dirt',
  textures: {
    default: 'dirt',
    2: 'grass', // +Y top face
  },
})
