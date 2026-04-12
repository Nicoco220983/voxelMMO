// @ts-check
import { VoxelType } from '../VoxelTypes.js'
import { createVoxel } from './BaseVoxel.js'

export const BasicVoxel = createVoxel({
  type: VoxelType.BASIC,
  name: 'basic',
  textures: 'basic',
})
