#include "voxel.h"
#include "base.h"
#include "linalg.h"
#include "memory.h"

#include "SDL_rwops.h"

#define OGT_VOX_IMPLEMENTATION
#include "ogt_vox.h"

int
lida_VoxelGridAllocate(lida_VoxelGrid* grid, uint32_t w, uint32_t h, uint32_t d)
{
  lida_Voxel* old_data = grid->data;
  grid->data = (lida_Voxel*)lida_TempAllocate(w*h*d);
  if (grid->data == NULL) {
    grid->data = old_data;
    return -1;
  }
  if (old_data) {
    for (uint32_t i = 0; i < grid->depth; i++) {
      for (uint32_t j = 0; j < grid->height; j++) {
        memcpy(grid->data + i*w*h + j*w,
               old_data + i*grid->width*grid->height + j*grid->width,
               grid->width);
      }
    }
    lida_MallocFree(old_data);
  } else {
    memset(grid->data, 0, w*h*d);
  }
  grid->width = w;
  grid->height = h;
  grid->depth = d;
  // grid->hash = lida_HashString64(grid->data);
  return 0;
}

void
lida_VoxelGridFree(lida_VoxelGrid* grid)
{
  if (grid->data) {
    lida_MallocFree(grid->data);
    grid->data = NULL;
  }
}

static const lida_Vec3 vox_positions[] = {
  // -x
  {0.0f, 1.0f, 1.0f},
  {0.0f, 1.0f, 0.0f},
  {0.0f, 0.0f, 0.0f},
  {0.0f, 0.0f, 0.0f},
  {0.0f, 0.0f, 1.0f},
  {0.0f, 1.0f, 1.0f},
  // +x
  {1.0f, 1.0f, 1.0f},
  {1.0f, 0.0f, 0.0f},
  {1.0f, 1.0f, 0.0f},
  {1.0f, 0.0f, 0.0f},
  {1.0f, 1.0f, 1.0f},
  {1.0f, 0.0f, 1.0f},
  // -y
  {0.0f, 0.0f, 0.0f},
  {1.0f, 0.0f, 0.0f},
  {1.0f, 0.0f, 1.0f},
  {1.0f, 0.0f, 1.0f},
  {0.0f, 0.0f, 1.0f},
  {0.0f, 0.0f, 0.0f},
  // +y
  {0.0f, 1.0f, 0.0f},
  {1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 0.0f},
  {1.0f, 1.0f, 1.0f},
  {0.0f, 1.0f, 0.0f},
  {0.0f, 1.0f, 1.0f},
  // -z
  {0.0f, 0.0f, 0.0f},
  {1.0f, 1.0f, 0.0f},
  {1.0f, 0.0f, 0.0f},
  {1.0f, 1.0f, 0.0f},
  {0.0f, 0.0f, 0.0f},
  {0.0f, 1.0f, 0.0f},
  // +z
  {0.0f, 0.0f, 1.0f},
  {1.0f, 0.0f, 1.0f},
  {1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 1.0f},
  {0.0f, 1.0f, 1.0f},
  {0.0f, 0.0f, 1.0f}
};

static const lida_iVec3 vox_normals[6] = {
  {-1, 0, 0},
  {1, 0, 0},
  {0, -1, 0},
  {0, 1, 0},
  {0, 0, -1},
  {0, 0, 1}
};

uint32_t
lida_VoxelGridGenerateMeshNaive(lida_VoxelGrid* grid, float scale, lida_VoxelVertex* vertices, int face)
{
  (void)grid;
  (void)vertices;
  lida_VoxelVertex* begin = vertices;
  lida_Vec3 half_size = {
    grid->width * scale * 0.5f,
    grid->height * scale * 0.5f,
    grid->depth * scale * 0.5f
  };
  int offsetX = vox_normals[face].x;
  int offsetY = vox_normals[face].y;
  int offsetZ = vox_normals[face].z;
  // yes, we just loop over all voxels
  // note how we firstly iterate over x for better cache locality
  for (uint32_t z = 0; z < grid->depth; z++)
    for (uint32_t y = 0; y < grid->height; y++)
      for (uint32_t x = 0; x < grid->width; x++) {
        lida_Voxel voxel = lida_VoxelGridGet(grid, x, y, z);
        lida_Voxel near_voxel = lida_VoxelGridGet(grid, x+offsetX, y+offsetY, z+offsetZ);
        if (// check if voxel is not air
            voxel &&
            // check if near voxel is air
            near_voxel == 0) {
          lida_Vec3 pos = lida_Vec3{(float)x, (float)y, (float)z} * scale - half_size;
          for (uint32_t i = 0; i < 6; i++) {
            vertices[i].position = pos + vox_positions[face*6 + i] * scale;
            vertices[i].color = grid->palette[voxel];
          }
          vertices += 6;
        }
      }
  return vertices - begin;
}

uint32_t
lida_VoxelGridGenerateMeshGreedy(lida_VoxelGrid* grid, lida_VoxelVertex* vertices)
{
  (void)grid;
  (void)vertices;
  return 0;
}

int
lida_VoxelGridLoad(lida_VoxelGrid* grid, const uint8_t* buffer, uint32_t size)
{
  const ogt_vox_scene* scene = ogt_vox_read_scene(buffer, size);
  if (scene == NULL) {
    LIDA_LOG_WARN("failed to parse voxel model");
    return -1;
  }
  const ogt_vox_model* model = scene->models[0];
  lida_VoxelGridAllocate(grid, model->size_x, model->size_y, model->size_z);
  memcpy(grid->palette, scene->palette.color, 256 * sizeof(ogt_vox_rgba));
  for (uint32_t x = 0; x < grid->width; x++) {
    for (uint32_t y = 0; y < grid->height; y++) {
      for (uint32_t z = 0; z < grid->depth; z++) {
        uint32_t index = x + z*grid->width + y*grid->width*grid->depth;
        lida_Voxel voxel = model->voxel_data[index];
        if (voxel)
          lida_VoxelGridGet(grid, x, y, z) = voxel;
      }
    }
  }
  ogt_vox_destroy_scene(scene);
  return 0;
}

int
lida_VoxelGridLoadFromFile(lida_VoxelGrid* grid, const char* filename)
{
  size_t buff_size;
  uint8_t* buffer = (uint8_t*)SDL_LoadFile(filename, &buff_size);
  if (buffer == NULL) {
    LIDA_LOG_WARN("failed to open file '%s' for voxel model loading", filename);
    return -1;
  }
  int ret = lida_VoxelGridLoad(grid, buffer, buff_size);
  SDL_free(buffer);
  return ret;
}
