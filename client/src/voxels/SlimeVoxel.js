// @ts-check
import { VoxelType } from '../VoxelTypes.js'
import { createVoxel } from './BaseVoxel.js'

export const SlimeVoxel = createVoxel({
  type: VoxelType.SLIME,
  name: 'slime',
  textures: 'slime',
})
