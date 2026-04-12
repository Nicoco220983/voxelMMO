// @ts-check
import { VoxelType } from '../VoxelTypes.js'
import { createVoxel } from './BaseVoxel.js'

export const PlanksVoxel = createVoxel({
  type: VoxelType.PLANKS,
  name: 'planks',
  textures: 'planks',
})
