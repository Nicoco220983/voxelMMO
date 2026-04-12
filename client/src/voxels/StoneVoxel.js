// @ts-check
import { VoxelType } from '../VoxelTypes.js'
import { createVoxel } from './BaseVoxel.js'

export const StoneVoxel = createVoxel({
  type: VoxelType.STONE,
  name: 'stone',
  textures: 'stone',
})
