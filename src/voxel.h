#pragma once

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
  struct { uint8_t r, g, b, a; } palette[256];
} lida_VoxelGrid;

typedef struct {
  float x, y, z;
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

int lida_VoxelGridCreate(uint32_t w, uint32_t h, uint32_t d);
void lida_VoxelGridDestroy(uint32_t w, uint32_t h, uint32_t d);
lida_Voxel lida_VoxelGridGet(uint32_t x, uint32_t y, uint32_t z);

uint32_t lida_VoxelGridGenerateMeshBad(lida_VoxelVertex* vertices, uint32_t* indices);
uint32_t lida_VoxelGridGenerateMeshGood(lida_VoxelVertex* vertices, uint32_t* indices);

int lida_VoxelGridLoad(lida_VoxelGrid* grid, const uint8_t* buffer, uint32_t size);
int lida_VoxelGridLoadFromFile(lida_VoxelGrid* grid, const char* filename);

#ifdef __cplusplus
}
#endif
