#pragma once

#include "linalg.h"
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t lida_Voxel;

typedef struct {
  lida_Voxel* data;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint64_t hash;
  uint32_t palette[256];
} lida_VoxelGrid;

typedef struct {
  lida_Vec3 position;
  uint32_t color;
} lida_VoxelVertex;

typedef struct {
  uint64_t hash;
  uint32_t num_vertices;
  float scale;
} lida_VoxelMesh;

typedef struct {
  // this is for vkCmdDraw
  uint32_t vertexCount;
  uint32_t firstVertex;
  uint32_t firstInstance;
  // this is for culling
  /* vec3 normalVector; */
  /* vec3 position; */
} lida_VoxelDrawCommand;

typedef struct {
  lida_VideoMemory memory;
  VkBuffer vertex_buffer;
  VkBuffer index_buffer;
  VkBuffer storage_buffer;
  lida_VoxelVertex* pVertices;
  // Transform* transforms;
  struct {
    uint32_t num_indices;
    uint32_t num_draws;
    // lida_Array draws;
    // lida_Array cache;
    uint32_t vertex_flush_offset;
  } frames[2];
} lida_VoxelDrawer;

int lida_VoxelGridAllocate(lida_VoxelGrid* grid, uint32_t w, uint32_t h, uint32_t d);
void lida_VoxelGridFree(lida_VoxelGrid* grid);
#define lida_VoxelGridGet(grid, x, y, z) (grid)->data[(x) + (y)*(grid)->width + (z)*(grid)->width*(grid)->height]

uint32_t lida_VoxelGridGenerateMeshNaive(lida_VoxelGrid* grid, float size, lida_VoxelVertex* vertices, int face);
uint32_t lida_VoxelGridGenerateMeshGreedy(lida_VoxelGrid* grid, lida_VoxelVertex* vertices);

int lida_VoxelGridLoad(lida_VoxelGrid* grid, const uint8_t* buffer, uint32_t size);
int lida_VoxelGridLoadFromFile(lida_VoxelGrid* grid, const char* filename);

#ifdef __cplusplus
}
#endif
