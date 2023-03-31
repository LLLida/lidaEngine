/*
  Procedural generation with voxels.

  Math goes brrrrrrrrrrrrrrrrrrrrr...
 */


/// Some silly shape

INTERNAL void
GenerateFractal1(EID entity)
{
  Voxel_Grid* grid = GetComponent(Voxel_Grid, entity);
  const uint32_t size = 64;
  AllocateVoxelGrid(g_vox_allocator, grid, size, size, size);
  for (uint32_t offset = 0; offset < 20; offset += 2) {
    Voxel color = offset>>1;

    for (uint32_t x = offset; x < grid->width-offset; x++) {
      uint32_t y = grid->height-offset-1;
      uint32_t z = grid->depth-offset-1;
      GetInVoxelGrid(grid, x, offset, offset) = color;
      GetInVoxelGrid(grid, x, offset, z)      = color;
      GetInVoxelGrid(grid, x, y,      offset) = color;
      GetInVoxelGrid(grid, x, y,      z)      = color;
    }
    for (uint32_t y = offset; y < grid->height-offset; y++) {
      uint32_t x = grid->width-offset-1;
      uint32_t z = grid->depth-offset-1;
      GetInVoxelGrid(grid, offset, y, offset) = color;
      GetInVoxelGrid(grid, offset, y, z)      = color;
      GetInVoxelGrid(grid, x,      y, offset) = color;
      GetInVoxelGrid(grid, x,      y, z)      = color;
    }
    for (uint32_t z = offset; z < grid->depth-offset; z++) {
      uint32_t x = grid->width-offset-1;
      uint32_t y = grid->height-offset-1;
      GetInVoxelGrid(grid, offset, offset, z) = color;
      GetInVoxelGrid(grid, offset, y,      z) = color;
      GetInVoxelGrid(grid, x,      offset, z) = color;
      GetInVoxelGrid(grid, x,      y,      z) = color;
    }
  }
}


/// Menger sponge
/// https://en.wikipedia.org/wiki/Menger_sponge

// recursive function to do nasty thing
INTERNAL void
Fractal2_Helper(Voxel_Grid* grid, uVec3 pos, uint32_t size)
{
  if (size == 1) {
    GetInVoxelGrid(grid, pos.x, pos.y, pos.z) = 1;
    return;
  }
  for (uint32_t i = 0; i < 3; i++)
    for (uint32_t j = 0; j < 3; j++)
      for (uint32_t k = 0; k < 3; k++) {
        int count = 0;
        count += i == 1;
        count += j == 1;
        count += k == 1;
        if (count >= 2) continue;
        uVec3 npos = pos;
        npos.x += i * size / 3;
        npos.y += j * size / 3;
        npos.z += k * size / 3;
        Fractal2_Helper(grid, npos, size/3);
      }
}

// NOTE: don't pass level > 5 or your computer will die
INTERNAL void
GenerateFractal2(EID entity, uint32_t level)
{
   Voxel_Grid* grid = GetComponent(Voxel_Grid, entity);
   uint32_t size = 1;
   while (level--) {
     size *= 3;
   }
   AllocateVoxelGrid(g_vox_allocator, grid, size, size, size);
   FillVoxelGrid(grid, 0);
   Fractal2_Helper(grid, (uVec3){0, 0, 0}, size);
}


/// TODO: do tree generation
