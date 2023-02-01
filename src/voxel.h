#pragma once

#include "base.h"
#include "linalg.h"
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t lida_Voxel;

// stores voxels as plain 3D array
typedef struct {

  lida_Voxel* data;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint64_t hash;
  uint32_t palette[256];

} lida_VoxelGrid;

// 16 bytes
typedef struct {

  lida_Vec3 position;
  uint32_t color;

} lida_VoxelVertex;

typedef struct {

  lida_VideoMemory memory;
  VkBuffer vertex_buffer;
  VkBuffer transform_buffer;
  // VkBuffer index_buffer;
  lida_VoxelVertex* pVertices;
  lida_Transform* pTransforms;
  uint32_t max_vertices;
  uint32_t max_draws;
  int frame_id;
  uint32_t vertex_offset;
  uint32_t transform_offset;
  lida_VoxelVertex* vertex_temp_buffer;
  uint32_t vertex_temp_buffer_size;
  struct {
    lida_DynArray draws;
    lida_DynArray meshes;
  } frames[2];
  lida_DynArray hashes_cached;
  lida_DynArray regions_cached;

} lida_VoxelDrawer;

int lida_VoxelGridAllocate(lida_VoxelGrid* grid, uint32_t w, uint32_t h, uint32_t d);
int lida_VoxelGridReallocate(lida_VoxelGrid* grid, uint32_t w, uint32_t h, uint32_t d);
void lida_VoxelGridFree(lida_VoxelGrid* grid);
void lida_VoxelGridFreeWrapper(void* grid);
// note: setting a voxel value with this macro is unsafe, hash won't be correct,
// consider using lida_VoxelGridSet
#define lida_VoxelGridGet(grid, x, y, z) (grid)->data[(x) + (y)*(grid)->width + (z)*(grid)->width*(grid)->height]
void lida_VoxelGridSet(lida_VoxelGrid* grid, uint32_t x, uint32_t y, uint32_t z, lida_Voxel vox);

uint32_t lida_VoxelGridMaxGeneratedVertices(const lida_VoxelGrid* grid);
// this works fast and doesn't allocate any memory,
// but generates poor meshes with a lot unnecessary vertices.
uint32_t lida_VoxelGridGenerateMeshNaive(const lida_VoxelGrid* grid, lida_VoxelVertex* vertices, int face);
// this works relatively slow and does allocate some memory temporarily.
// generated meshes are slow and GPU is happy with them.
uint32_t lida_VoxelGridGenerateMeshGreedy(const lida_VoxelGrid* grid, lida_VoxelVertex* vertices, int face);

// parse voxel grid from buffer with voxels encoded in '.vox' Magica voxel format.
int lida_VoxelGridLoad(lida_VoxelGrid* grid, const uint8_t* buffer, uint32_t size);
// load a file to buffer and use 'lida_VoxelGridLoad' on it.
int lida_VoxelGridLoadFromFile(lida_VoxelGrid* grid, const char* filename);

VkResult lida_VoxelDrawerCreate(lida_VoxelDrawer* drawer, uint32_t max_vertices, uint32_t max_draws);
void lida_VoxelDrawerDestroy(lida_VoxelDrawer* drawer);
// prepare voxel drawer for recording draw commands and writing vertices.
void lida_VoxelDrawerNewFrame(lida_VoxelDrawer* drawer);
//
void lida_VoxelDrawerPushMesh(lida_VoxelDrawer* drawer, const lida_VoxelGrid* grid, const lida_Transform* transform);
// draw all voxels recorded with 'lida_VoxelDrawerPushMesh' in current frame.
void lida_VoxelDrawerDraw(lida_VoxelDrawer* drawer, VkCommandBuffer cmd);
void lida_VoxelDrawerDrawWithNormals(lida_VoxelDrawer* drawer, VkCommandBuffer cmd, uint32_t normal_id);

void lida_PipelineVoxelVertices(const VkVertexInputAttributeDescription** attributes, uint32_t* num_attributes,
                                const VkVertexInputBindingDescription** bindings, uint32_t* num_bindings);

#ifdef __cplusplus
}
#endif
