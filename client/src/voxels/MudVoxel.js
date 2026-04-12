// @ts-check
import { VoxelType } from '../VoxelTypes.js'
import { createVoxel } from './BaseVoxel.js'

export const MudVoxel = createVoxel({
  type: VoxelType.MUD,
  name: 'mud',
  textures: 'mud',
})
