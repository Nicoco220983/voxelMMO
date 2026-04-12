// @ts-check
import { VoxelType } from '../VoxelTypes.js'
import { createVoxel } from './BaseVoxel.js'

export const BricksVoxel = createVoxel({
  type: VoxelType.BRICKS,
  name: 'bricks',
  textures: 'bricks',
})
