/*
  lida_voxel.c

  Voxel loading, creation, rendering etc.
  https://www.youtube.com/watch?v=dQw4w9WgXcQ
 */

#define VX_USE_INDICES 1
#define VX_USE_CULLING 1
#define VX_USE_BLOCKS 0
#define MAX_ACTIVE_CAMERAS 8
#define VOXEL_VERTEX_THRESHOLD 8*1024

typedef uint8_t Voxel;

typedef Voxel Voxel_Block[64];

// stores voxels as plain 3D array
typedef struct {

  Allocation* data;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint64_t hash;
  uint32_t palette[256];
  uint64_t last_hash;
  uint32_t first_vertex;
  uint32_t offsets[6];

} Voxel_Grid;
DECLARE_COMPONENT(Voxel_Grid);

typedef struct {

  EID grid;
  int cull_mask;

} Voxel_View;
DECLARE_COMPONENT(Voxel_View);

typedef struct {

  Vec3 half_size;
  uint32_t first_vertex;
  // NOTE: we don't use instancing currently, should we implement support for it?
  uint32_t first_instance;
  uint32_t vertex_count[6];
  uint32_t cull_mask;

} VX_Draw_Data;

typedef struct {

  uint32_t count0;
  uint32_t count1;
  uint32_t count2;
  uint32_t count3;
  uint32_t count4;
  uint32_t debug_data1;
  uint32_t debug_data2;
  uint32_t debug_data3;

} VX_Vertex_Count;

typedef struct {

  // this is for vkCmdDraw
  uint32_t vertexCount;
  uint32_t firstVertex;
  uint32_t firstInstance;

} VX_Draw_Command;

typedef struct {

  VkBuffer vertex_buffer;
  VkBuffer transform_buffer;
  VkBuffer vertex_count_buffer;
  VkBuffer index_buffer;

  Vertex_X3C* pVertices;
  Transform* pTransforms;
  VX_Vertex_Count* pVertexCounts;
  uint32_t* pIndices;

  // reset each frame
  size_t vertex_offset;
  size_t transform_offset;
  size_t start_transform_offset;
  Allocation* draws;
  size_t num_draws;
  size_t num_vertices;

  Allocation* meshes;
  size_t num_meshes;

} Voxel_Backend_Slow;

typedef struct {

  VkBuffer vertex_buffer;
  VkBuffer transform_buffer;
  VkBuffer index_buffer;
  VkBuffer storage_buffer;
  VkBuffer indirect_buffer;
  VkBuffer vertex_count_buffer;
  // used for compute preprocessing
  VkDescriptorSet ds_set;

  VX_Draw_Data* pDraws;
  Vertex_X3C* pVertices;
  Transform* pTransforms;
  uint32_t* pIndices;

  // reset each frame
  size_t vertex_offset;
  size_t transform_offset;
  size_t start_transform_offset;
  size_t draw_offset;
  size_t num_vertices;

  int enabled_KHR_draw_indirect_count;

} Voxel_Backend_Indirect;

typedef struct {

  Video_Memory cpu_memory;
  Video_Memory gpu_memory;

  size_t max_vertices;
  size_t max_draws;
#if VX_USE_INDICES
  size_t max_indices;
#endif
  size_t num_draws;

  union {
    Voxel_Backend_Slow slow;
    Voxel_Backend_Indirect indirect;
  } backend;
  Pipeline_Stats pipeline_stats_fragment;
  Pipeline_Stats pipeline_stats_shadow;

  void (*new_frame_func)(void* backend);
  void (*clear_cache_func)(void* backend);
  void (*regenerate_mesh_func)(void* backend, Voxel_View* cached, Voxel_Grid* grid);
  void (*push_mesh_func)(void* backend, EID entity);
  uint32_t (*render_voxels_func)(void* backend, VkCommandBuffer cmd, const Camera* camera, uint32_t num_sets, VkDescriptorSet* sets, size_t num_draws);
  void (*cull_pass_func)(void* backend, VkCommandBuffer cmd, const Camera* cameras, uint32_t num_cameras, size_t num_draws);
  void (*destroy_func)(void* backend, Deletion_Queue* dq);
  uint32_t (*stat_func)(void* backend, char* buff);

} Voxel_Drawer;

Voxel_Drawer* g_vox_drawer;

#define VOX_BLOCK_DIM(x) ((x)>>2)
#define VOX_DIM_IN_VOXEL(x) ((x)&3)
#define GetVoxelBlock(grid, x, y, z) ((Voxel_Block*)(grid)->data->ptr)[VOX_BLOCK_DIM(x) + VOX_BLOCK_DIM(y)*VOX_BLOCK_DIM((grid)->width) + VOX_BLOCK_DIM(z)*VOX_BLOCK_DIM((grid)->width)*VOX_BLOCK_DIM((grid)->height)]

#if VX_USE_BLOCKS
// #define GetInVoxelGrid(grid, x, y, z) (GetVoxelBlock(grid, x, y, z))[VOX_DIM_IN_VOXEL(x) + (VOX_DIM_IN_VOXEL(y)<<2) + (VOX_DIM_IN_VOXEL(z)<<4)]

INTERNAL Voxel*
GetInVoxelGridImpl(const Voxel_Grid* grid, uint32_t x, uint32_t y, uint32_t z)
{
  Voxel* start = grid->data->ptr;
  uint32_t dimw = ALIGN_TO(grid->width, 4);
  dimw = VOX_BLOCK_DIM(dimw);
  uint32_t dimh = ALIGN_TO(grid->height, 4);
  dimh = VOX_BLOCK_DIM(dimh);
  Voxel* block = start + (VOX_BLOCK_DIM(x) + VOX_BLOCK_DIM(y)*dimw + dimw*dimh*VOX_BLOCK_DIM(z))*64;
  return block + (VOX_DIM_IN_VOXEL(x) + VOX_DIM_IN_VOXEL(y)*4 + VOX_DIM_IN_VOXEL(z)*16);
}
#define GetInVoxelGrid(grid, x, y, z) (*GetInVoxelGridImpl(grid, x, y, z))

#else
// NOTE: this doesn't do bounds checking
// NOTE: setting a voxel value with this macro is unsafe, hash won't be correct,
// consider using SetInVoxelGrid
#define GetInVoxelGrid(grid, x, y, z) ((Voxel*)(grid)->data->ptr)[(x) + (y)*(grid)->width + (z)*(grid)->width*(grid)->height]
#endif

Allocator* g_vox_allocator;

EID g_voxel_pipeline_colored;
EID g_voxel_pipeline_shadow;
EID g_voxel_pipeline_compute_ortho;
EID g_voxel_pipeline_compute_persp;
EID g_voxel_pipeline_compute_ext_ortho;
EID g_voxel_pipeline_compute_ext_persp;

// TODO: compress voxels on disk using RLE(Run length Encoding)


/// Voxel grid

// CLEANUP: do we really need this function? I think it's better to
// just recreate voxel grids when needed.
INTERNAL int
AllocateVoxelGrid(Allocator* allocator, Voxel_Grid* grid, uint32_t w, uint32_t h, uint32_t d)
{
#if VX_USE_BLOCKS
  uint32_t wa = ALIGN_TO(w, 4);
  uint32_t ha = ALIGN_TO(h, 4);
  uint32_t da = ALIGN_TO(d, 4);
  grid->data = DoAllocation(allocator, wa*ha*da, "voxel-grid");
  if (grid->data == NULL) {
    LOG_WARN("out of memory");
    return -1;
  }
  memset(grid->data->ptr, 0, wa*ha*da);
  grid->width = w;
  grid->height = h;
  grid->depth = d;
  grid->first_vertex = UINT32_MAX;
  return 0;
#else
  grid->data = DoAllocation(allocator, w*h*d, "voxel-grid");
  if (grid->data == NULL) {
    LOG_WARN("out of memory");
    return -1;
  }
  // In some cases this memset may be redundant.
  // Do we need to introduce some kind of boolean argument for this function?
  memset(grid->data->ptr, 0, w*h*d);
  grid->width = w;
  grid->height = h;
  grid->depth = d;
  grid->first_vertex = UINT32_MAX;
  return 0;
#endif
}

INTERNAL void
FreeVoxelGrid(Allocator* allocator, Voxel_Grid* grid)
{
  if (grid->data) {
    FreeAllocation(allocator, grid->data);
    grid->data = NULL;
  }
}

/**
   Get number of bytes occupied by a voxel grid's data.
 */
INTERNAL uint32_t
VoxelGridBytes(const Voxel_Grid* grid)
{
#if VX_USE_BLOCKS
  uint32_t wa = ALIGN_TO(grid->width, 4);
  uint32_t ha = ALIGN_TO(grid->height, 4);
  uint32_t da = ALIGN_TO(grid->depth, 4);
  return wa*ha*da;
#else
  return grid->width * grid->height * grid->depth;
#endif
}

INTERNAL void
RehashVoxelGrid(Voxel_Grid* grid)
{
  grid->hash = HashMemory64(grid->data->ptr, VoxelGridBytes(grid));
}

INTERNAL void
SetInVoxelGrid(Voxel_Grid* grid, uint32_t x, uint32_t y, uint32_t z, Voxel vox)
{
  GetInVoxelGrid(grid, x, y, z) = vox;
  uint64_t hashes[2] = { grid->hash, (uint64_t)vox };
  grid->hash = HashCombine64(hashes, 2);
}

GLOBAL const Vec3 vox_positions[] = {
#if VX_USE_INDICES
  // -x
  {0.0f, 1.0f, 1.0f},
  {0.0f, 1.0f, 0.0f},
  {0.0f, 0.0f, 0.0f},
  {0.0f, 0.0f, 1.0f},
  // +x
  {1.0f, 1.0f, 0.0f},
  {1.0f, 1.0f, 1.0f},
  {1.0f, 0.0f, 1.0f},
  {1.0f, 0.0f, 0.0f},
  // -y
  {1.0f, 0.0f, 0.0f},
  {1.0f, 0.0f, 1.0f},
  {0.0f, 0.0f, 1.0f},
  {0.0f, 0.0f, 0.0f},
  // +y
  {1.0f, 1.0f, 1.0f},
  {1.0f, 1.0f, 0.0f},
  {0.0f, 1.0f, 0.0f},
  {0.0f, 1.0f, 1.0f},
  // -z
  {1.0f, 1.0f, 0.0f},
  {1.0f, 0.0f, 0.0f},
  {0.0f, 0.0f, 0.0f},
  {0.0f, 1.0f, 0.0f},
  // +z
  {1.0f, 0.0f, 1.0f},
  {1.0f, 1.0f, 1.0f},
  {0.0f, 1.0f, 1.0f},
  {0.0f, 0.0f, 1.0f},
#else
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
#endif
};

GLOBAL const uint32_t vox_indices[6] = { 0, 1, 2, 2, 3, 0 };

GLOBAL const iVec3 vox_normals[6] = {
  {-1, 0, 0},
  {1, 0, 0},
  {0, -1, 0},
  {0, 1, 0},
  {0, 0, -1},
  {0, 0, 1}
};

GLOBAL const Vec3 f_vox_normals[6] = {
  {-1.0f, 0.0f, 0.0f},
  {1.0f, 0.0f, 0.0f},
  {0.0f, -1.0f, 0.0f},
  {0.0f, 1.0f, 0.0f},
  {0.0f, 0.0f, -1.0f},
  {0.0f, 0.0f, 1.0f}
};

// return: inv_size
INTERNAL float
CalculateVoxelGridSize(const Voxel_Grid* grid, Vec3* half_size)
{
  float inv_size;
  // TODO: we currently have no Min() function defined... shame
  if (grid->width <= grid->height && grid->height <= grid->depth) {
    inv_size = 1.0f / (float)grid->width;
  } else if (grid->height <= grid->width && grid->width <= grid->depth) {
    inv_size = 1.0f / (float)grid->height;
  } else {
    inv_size = 1.0f / (float)grid->depth;
  }
  half_size->x = inv_size * 0.5f * (float)grid->width;
  half_size->y = inv_size * 0.5f * (float)grid->height;
  half_size->z = inv_size * 0.5f * (float)grid->depth;
  return inv_size;
}

INTERNAL uint32_t
GenerateVoxelGridMeshNaive(const Voxel_Grid* grid, Vertex_X3C* vertices, int face
#if VX_USE_INDICES
                           , uint32_t base_index, uint32_t* indices
#endif
                           )
{
  Vertex_X3C* const first_vertex = vertices;
  Vec3 half_size;
  float inv_size = CalculateVoxelGridSize(grid, &half_size);
  int offsetX = vox_normals[face].x;
  int offsetY = vox_normals[face].y;
  int offsetZ = vox_normals[face].z;
  // yes, we just loop over all voxels
  // note how we first iterate over x for better cache locality
  for (uint32_t z = 0; z < grid->depth; z++)
    for (uint32_t y = 0; y < grid->height; y++)
      for (uint32_t x = 0; x < grid->width; x++) {
        Voxel voxel = GetInVoxelGrid(grid, x, y, z);
        Voxel near_voxel;
        if (x+offsetX < grid->width &&
            y+offsetY < grid->height &&
            z+offsetZ < grid->depth) {
          near_voxel = GetInVoxelGrid(grid, x+offsetX, y+offsetY, z+offsetZ);
        } else {
          near_voxel = 0;
        }
        if (// check if voxel is not air
            voxel &&
            // check if near voxel is air
            near_voxel == 0) {
          Vec3 pos = VEC3_CREATE((float)x, (float)y, (float)z);
#if VX_USE_INDICES
           // write 4 vertices and 6 indices for this quad
          for (uint32_t i = 0; i < 6; i++) {
            *(indices++) = base_index + (vertices - first_vertex) + vox_indices[i];
          }
          for (uint32_t vert_index = 0; vert_index < 4; vert_index++) {
            *(vertices++) = (Vertex_X3C) {
              .position.x = (pos.x + vox_positions[face*4+vert_index].x) * inv_size - half_size.x,
              .position.y = (pos.y + vox_positions[face*4+vert_index].y) * inv_size - half_size.y,
              .position.z = (pos.z + vox_positions[face*4+vert_index].z) * inv_size - half_size.z,
              .color = grid->palette[voxel],
            };
          }
#else
          for (uint32_t vert_index = 0; vert_index < 6; vert_index++) {
            *(vertices++) = (Vertex_X3C) {
              .position.x = (pos.x + vox_positions[face*6 + vert_index].x) * inv_size - half_size.x,
              .position.y = (pos.y + vox_positions[face*6 + vert_index].y) * inv_size - half_size.y,
              .position.z = (pos.z + vox_positions[face*6 + vert_index].z) * inv_size - half_size.z,
              .color = grid->palette[voxel],
            };
          }
#endif
        }
      }
  LOG_DEBUG("wrote %u vertices", (uint32_t)(vertices - first_vertex));
  return vertices - first_vertex;
}

INTERNAL Voxel
GetInVoxelGridChecked(const Voxel_Grid* grid, uint32_t x, uint32_t y, uint32_t z)
{
  if (x < grid->width &&
      y < grid->height &&
      z < grid->depth) {
    return GetInVoxelGrid(grid, x, y, z);
  }
  return 0;
}

INTERNAL uint32_t
GenerateVoxelGridMeshGreedy(const Voxel_Grid* grid, Vertex_X3C* vertices, int face
#if VX_USE_INDICES
                            , uint32_t base_index, uint32_t* indices
#endif
                            )
{
  PROFILE_FUNCTION();
  // uint32_t start_time = PlatformGetTicks();
  // TODO: my dream is to make this function execute fast, processing
  // 4 or 8 voxels at same time
  Vertex_X3C* const first_vertex = vertices;
  Vec3 half_size;
  float inv_size = CalculateVoxelGridSize(grid, &half_size);
  const uint32_t dims[3] = { grid->width, grid->height, grid->depth };
  const int d = face >> 1;
  const int u = (d+1)%3, v = (d+2)%3;
#if 0
  // iterate each block
  Voxel* block = (Voxel*)grid->data->ptr;
  for (uint32_t gz = 0; gz < grid->depth; gz += 4)
    for (uint32_t gy = 0; gy < grid->height; gy += 4)
      for (uint32_t gx = 0; gx < grid->width; gx += 4) {
        for (uint32_t layer = 0; layer < 4; layer++) {
          uint16_t merged_mask = 0;
          // try to merge voxels in block
          for (uint32_t j = 0; j < 4; j++)
            for (uint32_t i = 0; i < 4; i++) {
              if (merged_mask & (1 << (i + j*4)))
                continue;
              int pos[3];
              pos[d] = layer;
              pos[u] = i;
              pos[v] = j;
              Voxel start_voxel = block[pos[0] + (pos[1] << 2) + (pos[2] << 4)];
              if (start_voxel == 0) {
                // skip air
                continue;
              }
              const int start_pos[3] = { pos[0], pos[1], pos[2] };
              int min_i = 4;
              while (pos[v] < 4) {
                pos[u] = i;
                // TODO: try to use local adressation
                if (block[pos[0] + (pos[1]<<2) + (pos[2]<<4)] != start_voxel ||
                    GetInVoxelGridChecked(grid,
                                          pos[0] + gx + vox_normals[face].x,
                                          pos[1] + gy + vox_normals[face].y,
                                          pos[2] + gz + vox_normals[face].z) != 0)
                  break;
                pos[u]++;
                while (pos[u] < min_i &&
                       block[pos[0] + (pos[1]<<2) + (pos[2]<<4)] == start_voxel &&
                       GetInVoxelGridChecked(grid,
                                             pos[0] + gx + vox_normals[face].x,
                                             pos[1] + gy + vox_normals[face].y,
                                             pos[2] + gz + vox_normals[face].z) == 0) {
                  pos[u]++;
                }
                if (pos[u] < min_i) min_i = pos[u];
                pos[v]++;
              }
              if (min_i == start_pos[u] ||
                  pos[v] == start_pos[v]) {
                continue;
              }
              int offset[3] = { 0 };
              offset[u] = min_i - start_pos[u];
              offset[v] = pos[v] - start_pos[v];
#if VX_USE_INDICES
              // write 4 vertices and 6 indices for this quad
              for (uint32_t i = 0; i < 6; i++) {
                *(indices++) = base_index + (vertices - first_vertex) + vox_indices[i];
              }
              for (uint32_t vert_index = 0; vert_index < 4; vert_index++) {
                int vert_pos[3] = { gz + start_pos[0] + offset[0] * (int)vox_positions[face*4 + vert_index].x,
                                    gy + start_pos[1] + offset[1] * (int)vox_positions[face*4 + vert_index].y,
                                    gx + start_pos[2] + offset[2] * (int)vox_positions[face*4 + vert_index].z };
                vert_pos[d] += face & 1;
                *(vertices++) = (Vertex_X3C) {
                  .position.x = vert_pos[0] * inv_size - half_size.x,
                  .position.y = vert_pos[1] * inv_size - half_size.y,
                  .position.z = vert_pos[2] * inv_size - half_size.z,
                  .color = grid->palette[start_voxel]
                };
              }
#else
              // write 6 vertices for this quad
              for (uint32_t vert_index = 0; vert_index < 6; vert_index++) {
                int vert_pos[3] = { gz + (int)start_pos[0] + (int)offset[0] * (int)vox_positions[face*6 + vert_index].x,
                                    gy + (int)start_pos[1] + (int)offset[1] * (int)vox_positions[face*6 + vert_index].y,
                                    gx + (int)start_pos[2] + (int)offset[2] * (int)vox_positions[face*6 + vert_index].z };
                vert_pos[d] += face & 1;
                vertices[vert_index].position.x = vert_pos[0] * inv_size - half_size.x;
                vertices[vert_index].position.y = vert_pos[1] * inv_size - half_size.y;
                vertices[vert_index].position.z = vert_pos[2] * inv_size - half_size.z;
                vertices[vert_index].color = grid->palette[start_voxel];
              }
              vertices += 6;
#endif
              // mark merged voxels
              for (int jj = j; jj < pos[v]; jj++)
                for (int ii = i; ii < min_i; ii++)
                  merged_mask |= (1 << (ii + jj*4));
            }
        }

        block += 64;
      }
#else
  char* merged_mask = (char*)PersistentAllocate(dims[u]*dims[v]);
  // on each layer we try to merge voxels as much as possible
  for (uint32_t layer = 0; layer < dims[d]; layer++) {
    // zero out mask
    memset(merged_mask, 0, dims[u]*dims[v]);
    for (uint32_t j = 0; j < dims[v]; j++)
      for (uint32_t i = 0; i < dims[u]; i++) {
        // if this voxel was already written then skip it.
        // this skip helps us to keep algorithm complexity at O(w*h*d).
        if (merged_mask[i + j*dims[u]])
          continue;
        uint32_t pos[3];
        pos[d] = layer;
        pos[u] = i;
        pos[v] = j;
        Voxel start_voxel = GetInVoxelGrid(grid, pos[0], pos[1], pos[2]);
        if (start_voxel == 0) {
          // we don't generate vertices for air
          continue;
        }
        const uint32_t start_pos[3] = { pos[0], pos[1], pos[2] };
        uint32_t min_i = dims[u];

        // grow quad while all voxels in that quad are the same and visible
        while (pos[v] < dims[v]) {
          pos[u] = i;
          if (GetInVoxelGrid(grid, pos[0], pos[1], pos[2]) != start_voxel ||
              GetInVoxelGridChecked(grid,
                                    pos[0] + vox_normals[face].x,
                                    pos[1] + vox_normals[face].y,
                                    pos[2] + vox_normals[face].z) != 0)
            break;
          pos[u]++;
          while (pos[u] < min_i &&
                 GetInVoxelGrid(grid, pos[0], pos[1], pos[2]) == start_voxel &&
                 GetInVoxelGridChecked(grid,
                                       pos[0] + vox_normals[face].x,
                                       pos[1] + vox_normals[face].y,
                                       pos[2] + vox_normals[face].z) == 0) {
            pos[u]++;
          }
          if (pos[u] < min_i) min_i = pos[u];
          pos[v]++;
        }
        if (min_i == start_pos[u] ||
            pos[v] == start_pos[v]) {
          continue;
        }
        uint32_t offset[3] = { 0 };
        offset[u] = min_i - start_pos[u]; // width of quad
        offset[v] = pos[v] - start_pos[v]; // height of quad
#if VX_USE_INDICES
        // write 4 vertices and 6 indices for this quad
        for (uint32_t i = 0; i < 6; i++) {
          *(indices++) = base_index + (vertices - first_vertex) + vox_indices[i];
        }
        for (uint32_t vert_index = 0; vert_index < 4; vert_index++) {
          int vert_pos[3] = { (int)start_pos[0] + (int)offset[0] * (int)vox_positions[face*4 + vert_index].x,
                              (int)start_pos[1] + (int)offset[1] * (int)vox_positions[face*4 + vert_index].y,
                              (int)start_pos[2] + (int)offset[2] * (int)vox_positions[face*4 + vert_index].z };
          vert_pos[d] += face & 1;
          *(vertices++) = (Vertex_X3C) {
            .position.x = vert_pos[0] * inv_size - half_size.x,
            .position.y = vert_pos[1] * inv_size - half_size.y,
            .position.z = vert_pos[2] * inv_size - half_size.z,
            .color = grid->palette[start_voxel]
          };
        }
#else
        // write 6 vertices for this quad
        for (uint32_t vert_index = 0; vert_index < 6; vert_index++) {
          int vert_pos[3] = { (int)start_pos[0] + (int)offset[0] * (int)vox_positions[face*6 + vert_index].x,
                              (int)start_pos[1] + (int)offset[1] * (int)vox_positions[face*6 + vert_index].y,
                              (int)start_pos[2] + (int)offset[2] * (int)vox_positions[face*6 + vert_index].z };
          vert_pos[d] += face & 1;
          vertices[vert_index].position.x = vert_pos[0] * inv_size - half_size.x;
          vertices[vert_index].position.y = vert_pos[1] * inv_size - half_size.y;
          vertices[vert_index].position.z = vert_pos[2] * inv_size - half_size.z;
          vertices[vert_index].color = grid->palette[start_voxel];
        }
        vertices += 6;
#endif
        // mark merged voxels
        for (uint32_t jj = j; jj < pos[v]; jj++)
          for (uint32_t ii = i; ii < min_i; ii++) {
            merged_mask[ii + jj * dims[u]] = 1;
          }
      }
  }
  PersistentRelease(merged_mask);
#endif
  // LOG_DEBUG("took %u ms to generate %u vertices", PlatformGetTicks() - start_time, (uint32_t)(vertices - first_vertex));
  return vertices - first_vertex;
}

INTERNAL int
LoadVoxelGrid(Allocator* allocator, Voxel_Grid* grid, const uint8_t* buffer, uint32_t size)
{
  PROFILE_FUNCTION();
  const ogt_vox_scene* scene = ogt_vox_read_scene(buffer, size);
  if (scene == NULL) {
    LOG_WARN("failed to parse voxel model");
    return -1;
  }
  const ogt_vox_model* model = scene->models[0];
  if (AllocateVoxelGrid(allocator, grid, model->size_x, model->size_z, model->size_y)) {
    // if out of memory
    return -1;
  }
  memcpy(grid->palette, scene->palette.color, 256 * sizeof(ogt_vox_rgba));
  for (uint32_t x = 0; x < grid->width; x++) {
    for (uint32_t y = 0; y < grid->height; y++) {
      for (uint32_t z = 0; z < grid->depth; z++) {
        uint32_t index = x + z*grid->width + y*grid->width*grid->depth;
        Voxel voxel = model->voxel_data[index];
        if (voxel)
          GetInVoxelGrid(grid, x, y, z) = voxel;
      }
    }
  }
  ogt_vox_destroy_scene(scene);
  RehashVoxelGrid(grid);
  return 0;
}

INTERNAL int
LoadVoxelGridFromFile(Allocator* allocator, Voxel_Grid* grid, const char* filename)
{
  PROFILE_FUNCTION();
  size_t buff_size;
  uint8_t* buffer = (uint8_t*)PlatformLoadEntireFile(filename, &buff_size);
  if (buffer == NULL) {
    LOG_WARN("failed to open file '%s' for voxel model loading", filename);
    return -1;
  }
  int ret = LoadVoxelGrid(allocator, grid, buffer, buff_size);
  PlatformFreeLoadedFile(buffer);
  return ret;
}


/// 'Slow' backend

INTERNAL VkResult
CreateVoxelBackend_Slow(void* backend, Video_Memory* cpu_memory, uint32_t max_vertices, uint32_t max_draws)
{
  Voxel_Backend_Slow* drawer = backend;
  drawer->vertex_offset = 0;
  drawer->transform_offset = 0;
  drawer->draws = DoAllocation(g_vox_allocator, 6 * max_draws * sizeof(VX_Draw_Command),
                               "voxel-draws");
  drawer->meshes = DoAllocation(g_vox_allocator, max_draws * sizeof(EID),
                                "voxel-mesh-ids");
  VkResult err;

  // create buffers
#define CREATE_BUFFER(name, bytes, usage, mark) do {                    \
    err = CreateBuffer(&drawer->name, bytes, usage, mark);              \
    if (err != VK_SUCCESS) {                                            \
      LOG_ERROR("slow backend: failed to create " #name " with error %s", ToString_VkResult(err)); \
      return err;                                                       \
    }                                                                   \
  } while (0)
  CREATE_BUFFER(vertex_buffer, max_vertices * sizeof(Vertex_X3C),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/vertex-buffer");
  CREATE_BUFFER(transform_buffer, max_draws * sizeof(Transform),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                "voxel-drawer/transform-buffer");
  CREATE_BUFFER(vertex_count_buffer, max_draws * sizeof(VX_Vertex_Count),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/vertex-count-buffer");
#if VX_USE_INDICES
  CREATE_BUFFER(index_buffer, max_vertices * 3 / 2 * sizeof(uint32_t),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "voxel-draw/index-buffer");
#endif
#undef CREATE_BUFFER

  // allocate memory for buffers
#if VX_USE_INDICES
  VkMemoryRequirements buffer_requirements[4];
#else
  VkMemoryRequirements buffer_requirements[3];
#endif
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_buffer, &buffer_requirements[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->transform_buffer, &buffer_requirements[1]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_count_buffer, &buffer_requirements[2]);
#if VX_USE_INDICES
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->index_buffer, &buffer_requirements[3]);
#endif
  VkMemoryRequirements requirements;
  MergeMemoryRequirements(buffer_requirements, ARR_SIZE(buffer_requirements), &requirements);
  const VkMemoryPropertyFlags required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  // try to allocate GPU memory accessible from CPU because it's fast
  err = ReallocateMemoryIfNeeded(cpu_memory, g_deletion_queue, &requirements,
                                 required_flags|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 "voxel-drawer/memory");
  if (err != VK_SUCCESS) {
    // didn't succeed, fallback to 'slow' memory
    err = ReallocateMemoryIfNeeded(cpu_memory, g_deletion_queue, &requirements,
                                   required_flags, "voxel-drawer/memory");
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to allocate video memory for voxels with error %s", ToString_VkResult(err));
      return err;
    }
  }

  // bind buffers to allocated memory
#define BIND_BUFFER(memory, buffer, requirements, mapped) do {          \
    err = BufferBindToMemory(memory, drawer->buffer,                    \
                             &requirements, mapped, NULL);              \
    if (err != VK_SUCCESS) {                                            \
      LOG_WARN("failed to bind " #buffer " to memory with error %s", ToString_VkResult(err)); \
    }                                                                   \
  } while (0)
  BIND_BUFFER(cpu_memory, vertex_buffer, buffer_requirements[0], (void**)&drawer->pVertices);
  BIND_BUFFER(cpu_memory, transform_buffer, buffer_requirements[1], (void**)&drawer->pTransforms);
  BIND_BUFFER(cpu_memory, vertex_count_buffer, buffer_requirements[2], (void**)&drawer->pVertexCounts);
#if VX_USE_INDICES
  BIND_BUFFER(cpu_memory, index_buffer, buffer_requirements[3], (void**)&drawer->pIndices);
#endif
#undef BIND_BUFFER
  return err;
}

/**
   Start a new frame.
   @param backend - pointer to Voxel_Backend_Slow
**/
INTERNAL void
NewFrameVoxel_Slow(void* backend)
{
  Voxel_Backend_Slow* drawer = backend;
  if ((g_window->frame_counter & 1) == 0) {
    drawer->transform_offset = 0;
  }
  drawer->num_draws = 0;
  drawer->num_meshes = 0;
  drawer->start_transform_offset = drawer->transform_offset;
  drawer->num_vertices = 0;
}

INTERNAL void
ClearCacheVoxel_Slow(void* backend)
{
  Voxel_Backend_Slow* drawer = backend;
  drawer->vertex_offset = 0;
}

INTERNAL void
RegenerateVoxel_Slow(void* backend, Voxel_View* cached, Voxel_Grid* grid)
{
  Voxel_Backend_Slow* drawer = backend;
  (void)cached;
  grid->last_hash = grid->hash;

  // skip this vertex if we're exceeding threshold...
  if (drawer->num_vertices >= VOXEL_VERTEX_THRESHOLD)
    return;

  VX_Draw_Command* current_draws = drawer->draws->ptr;
#if VX_USE_INDICES
  uint32_t base_index = 0;
#endif
  for (int i = 0; i < 6; i++) {
    VX_Draw_Command* command = &current_draws[drawer->num_draws++];

#if 1
    // use greedy meshing
# if VX_USE_INDICES
    uint32_t index_offset = drawer->vertex_offset*3/2;
    command->vertexCount = GenerateVoxelGridMeshGreedy(grid, drawer->pVertices + drawer->vertex_offset, i,
                                                       base_index, drawer->pIndices + index_offset);
    base_index += command->vertexCount;
# else
    command->vertexCount =
      GenerateVoxelGridMeshGreedy(grid, drawer->pVertices + drawer->vertex_offset, i);
# endif
#else
    // use naive meshing algorithm
# if VX_USE_INDICES
    uint32_t index_offset = drawer->vertex_offset*3/2;
    command->vertexCount = GenerateVoxelGridMeshNaive(grid, drawer->pVertices + drawer->vertex_offset, i,
                                                      base_index, drawer->pIndices + index_offset);
    base_index += command->vertexCount;
# else
    command->vertexCount =
      GenerateVoxelGridMeshNaive(grid, drawer->pVertices + drawer->vertex_offset, i);
# endif
#endif

    command->firstVertex = drawer->vertex_offset;
    if (i == 0) {
      grid->first_vertex = command->firstVertex;
    }
    command->firstInstance = drawer->transform_offset - drawer->start_transform_offset;
    drawer->vertex_offset += command->vertexCount;
    grid->offsets[i] = command->vertexCount;
    drawer->num_vertices += command->vertexCount;
  }
}

INTERNAL void
PushMeshVoxel_Slow(void* backend, EID entity)
{
  Voxel_Backend_Slow* drawer = backend;
  // add transform
  Transform* transform = GetComponent(Transform, entity);
  memcpy(&drawer->pTransforms[drawer->transform_offset],
         transform, sizeof(Transform));
  EID* meshes = drawer->meshes->ptr;
  VX_Draw_Command* draws = drawer->draws->ptr;

  // try to use cache
  Voxel_View* cached = GetComponent(Voxel_View, entity);
  assert(cached);
  Voxel_Grid* grid = GetComponent(Voxel_Grid, cached->grid);
  assert(grid);
  if (grid->last_hash != grid->hash || grid->first_vertex > drawer->vertex_offset) {
    // regenerate if grid changed
    RegenerateVoxel_Slow(drawer, cached, grid);
  } else {
    uint32_t vertex_offset = grid->first_vertex;
    for (uint32_t i = 0; i < 6; i++) {
      VX_Draw_Command* command = &draws[drawer->num_draws++];
      command->firstVertex = vertex_offset;
      command->vertexCount = grid->offsets[i];
      command->firstInstance = drawer->transform_offset - drawer->start_transform_offset;
      vertex_offset += grid->offsets[i];
    }
  }
  // write vertex count
  VX_Vertex_Count* table = &drawer->pVertexCounts[drawer->transform_offset];
  table->count0 = grid->first_vertex + grid->offsets[0];
  table->count1 = table->count0 + grid->offsets[1];
  table->count2 = table->count1 + grid->offsets[2];
  table->count3 = table->count2 + grid->offsets[3];
  table->count4 = table->count3 + grid->offsets[4];

  drawer->transform_offset++;
  meshes[drawer->num_meshes++] = entity;
}

INTERNAL uint32_t
RenderVoxels_Slow(void* backend, VkCommandBuffer cmd, const Camera* camera, uint32_t num_sets, VkDescriptorSet* sets, size_t num_draws)
{
  Voxel_Backend_Slow* drawer = backend;
  (void)num_draws; // FIXME: maybe we should get rid of num_draws argument
  Graphics_Pipeline* prog;
  // NOTE: we assume that if camera is perspective than it uses normals
  if (camera->type == CAMERA_TYPE_PERSP) {
    prog = GetComponent(Graphics_Pipeline, g_voxel_pipeline_colored);
  } else {
    prog = GetComponent(Graphics_Pipeline, g_voxel_pipeline_shadow);
  }
  // bind vertices
  VkDeviceSize offsets[] = { 0, drawer->start_transform_offset * sizeof(Transform), drawer->start_transform_offset * sizeof(VX_Vertex_Count) };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer, drawer->vertex_count_buffer };
  vkCmdBindVertexBuffers(cmd, 0, ARR_SIZE(buffers), buffers, offsets);

#if VX_USE_INDICES
  vkCmdBindIndexBuffer(cmd, drawer->index_buffer, 0, VK_INDEX_TYPE_UINT32);
#endif

  cmdBindGraphics(cmd, prog, num_sets, sets);

  VX_Draw_Command* draws = drawer->draws->ptr;
#if VX_USE_CULLING
  EID* meshes = drawer->meshes->ptr;
#endif
  uint32_t draw_calls = 0;

  VX_Draw_Command draw_commands[3];
  for (uint32_t i = 0; i < drawer->num_meshes; i++) {
    EID mesh = meshes[i];
    Voxel_View* cached = GetComponent(Voxel_View, mesh);
    if ((cached->cull_mask & camera->cull_mask) == 0) {
      continue;
    }
    Transform* transform = GetComponent(Transform, mesh);
    OBB* obb = GetComponent(OBB, mesh);
    const uint32_t points[24] = {
      4, 5, 6, 7, // -X
      0, 1, 2, 3, // +X
      2, 3, 6, 7, // -Y
      0, 1, 4, 5, // +Y
      1, 3, 5, 7, // -Z
      0, 2, 4, 6, // +Z
    };
    uint32_t last_written_vertex = UINT32_MAX;
    uint32_t draw_count = 0;
    for (uint32_t normal_id = 0; normal_id < 6; normal_id++) {
      VX_Draw_Command* command = &draws[i*6+normal_id];
#if VX_USE_CULLING
      // try to backface cull this face
      Vec3 dist;
      // Hoping that compiler will optimize this check out of loop
      if (camera->type & CAMERA_TYPE_PERSP) {
        Vec3 point;
        point.x = 0.25f * (obb->corners[points[normal_id*4]].x + obb->corners[points[normal_id*4+1]].x + obb->corners[points[normal_id*4+2]].x + obb->corners[points[normal_id*4+3]].x);
        point.y = 0.25f * (obb->corners[points[normal_id*4]].y + obb->corners[points[normal_id*4+1]].y + obb->corners[points[normal_id*4+2]].y + obb->corners[points[normal_id*4+3]].y);
        point.z = 0.25f * (obb->corners[points[normal_id*4]].z + obb->corners[points[normal_id*4+1]].z + obb->corners[points[normal_id*4+2]].z + obb->corners[points[normal_id*4+3]].z);
        dist = VEC3_SUB(point, camera->position);
      } else {
        dist = camera->front;
      }
      Vec3 normal = f_vox_normals[normal_id];
      RotateByQuat(&normal, &transform->rotation, &normal);
      float dot = VEC3_DOT(dist, normal);
      if (dot > 0)
        continue;
#endif
      // try to merge this draw call with previous one
      if (last_written_vertex == command->firstVertex) {
        draw_commands[draw_count-1].vertexCount += command->vertexCount;
        last_written_vertex += command->vertexCount;
      } else {
        memcpy(&draw_commands[draw_count++], command, sizeof(VX_Draw_Command));
        last_written_vertex = command->firstVertex + command->vertexCount;
      }
    }
    uint32_t vertex_offset = draws[i*6].firstVertex;
    for (uint32_t j = 0; j < draw_count; j++) {
      VX_Draw_Command* command = &draw_commands[j];
#if VX_USE_INDICES
      vkCmdDrawIndexed(cmd, command->vertexCount*3/2, 1,
                       command->firstVertex*3/2, vertex_offset, command->firstInstance);
#else
      vkCmdDraw(cmd,
                command->vertexCount, 1,
                command->firstVertex, command->firstInstance);
#endif
      draw_calls++;
    }
  }

  return draw_calls;
}

INTERNAL void
CullPass_Slow(void* backend, VkCommandBuffer cmd, const Camera* mesh_passes, uint32_t num_passes,
              size_t num_draws)
{
  // do nothing, culling happens when submitting draws
  (void)backend;
  (void)cmd;
  (void)mesh_passes;
  (void)num_passes;
  (void)num_draws;
}

INTERNAL void
DestroyVoxel_Slow(void* backend, Deletion_Queue* dq)
{
  Voxel_Backend_Slow* drawer = backend;
  FreeAllocation(g_vox_allocator, drawer->meshes);
  FreeAllocation(g_vox_allocator, drawer->draws);
  if (dq == NULL) {
#if VX_USE_INDICES
    vkDestroyBuffer(g_device->logical_device, drawer->index_buffer, NULL);
#endif
    vkDestroyBuffer(g_device->logical_device, drawer->vertex_count_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->transform_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->vertex_buffer, NULL);
  } else {
    AddForDeletion(dq, (uint64_t)drawer->index_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->vertex_count_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->transform_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->vertex_buffer, VK_OBJECT_TYPE_BUFFER);
  }
}

INTERNAL uint32_t
VoxelStatistics_Slow(void* backend, char* buff)
{
  Voxel_Backend_Slow* drawer = backend;
  uint32_t count = 0;
  count += stbsp_sprintf("[vertices: %u] ", buff, drawer->vertex_offset);
  count += stbsp_sprintf("[draws: %u] ", buff, drawer->num_meshes);
  return count;
}


/// 'Fast' backend with indirect drawing

INTERNAL VkResult
CreateVoxelBackend_Indirect(void* backend, Video_Memory* cpu_memory, Video_Memory* gpu_memory,
                            uint32_t max_vertices, uint32_t max_draws)
{
  Voxel_Backend_Indirect* drawer = backend;
  drawer->transform_offset = 0;
  drawer->vertex_offset = 0;
  drawer->enabled_KHR_draw_indirect_count = 0;
  for (uint32_t i = 0; i < g_device->num_enabled_device_extensions; i++) {
    if (strcmp(g_device->enabled_device_extensions[i], VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME) == 0) {
      drawer->enabled_KHR_draw_indirect_count = 1;
      break;
    }
  }

  // we occupy 2*max_draws in buffers
  max_draws *= 2;

#if !VX_USE_INDICES
  Assert(0 && "Indirect drawing without index buffers is not implemented.");
#endif
  VkResult err;
#define CREATE_BUFFER(name, bytes, usage, mark) do {                    \
    err = CreateBuffer(&drawer->name, bytes, usage, mark);              \
    if (err != VK_SUCCESS) {                                            \
      LOG_ERROR("indirect backend: failed to create " #name " with error %s", ToString_VkResult(err)); \
      return err;                                                       \
    }                                                                   \
  } while (0)
  CREATE_BUFFER(vertex_buffer, max_vertices * sizeof(Vertex_X3C),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/vertex-buffer");
  CREATE_BUFFER(transform_buffer, max_draws * sizeof(Transform),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                "voxel-drawer/transform-buffer");
  CREATE_BUFFER(index_buffer, max_vertices * 3 / 2 * sizeof(uint32_t),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "voxel-draw/index-buffer");
  CREATE_BUFFER(storage_buffer, max_draws * sizeof(VX_Draw_Data),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "voxel-drawer/storage-buffer");
  uint32_t indirect_flags = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  if (drawer->enabled_KHR_draw_indirect_count) {
    // we'll be using vkCmdFillBuffer for filling this buffer with zeros
    indirect_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }
  // NOTE: 3 is max number of draw calls produced by a single model
  CREATE_BUFFER(indirect_buffer, MAX_ACTIVE_CAMERAS * 3 * max_draws * 32/*sizeof(VkDrawIndexedIndirectCommand)*/,
                indirect_flags, "voxel-drawer/indirect-buffer");
  CREATE_BUFFER(vertex_count_buffer, MAX_ACTIVE_CAMERAS * max_draws * sizeof(VX_Vertex_Count),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                "voxel-drawer/vertex-count-buffer");
#undef CREATE_BUFFER

  VkMemoryRequirements cpu_requirements[4];
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_buffer, &cpu_requirements[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->transform_buffer, &cpu_requirements[1]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->index_buffer, &cpu_requirements[2]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->storage_buffer, &cpu_requirements[3]);
  VkMemoryRequirements requirements;
  MergeMemoryRequirements(cpu_requirements, ARR_SIZE(cpu_requirements), &requirements);
  const VkMemoryPropertyFlags required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  err = ReallocateMemoryIfNeeded(cpu_memory, g_deletion_queue, &requirements,
                                 required_flags|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 "voxel-drawer/memory");
  if (err != VK_SUCCESS) {
    err = ReallocateMemoryIfNeeded(cpu_memory, g_deletion_queue, &requirements,
                                   required_flags, "voxel-drawer/memory");
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to allocate video memory for voxels with error %s", ToString_VkResult(err));
      return err;
    }
  }

  VkMemoryRequirements gpu_requirements[2];
  // allocate device local memory for buffers that we will not be accessing from CPU
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->indirect_buffer, &gpu_requirements[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_count_buffer, &gpu_requirements[1]);
  MergeMemoryRequirements(gpu_requirements, 2, &requirements);
  err = ReallocateMemoryIfNeeded(gpu_memory, g_deletion_queue, &requirements,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 "voxel-drawer/fast-memory");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate video memory for voxels with error %s", ToString_VkResult(err));
    return err;
  }

  // bind buffers to allocated memory
#define BIND_BUFFER(memory, buffer, requirements, mapped) do {          \
    err = BufferBindToMemory(memory, drawer->buffer,                    \
                             &requirements, mapped, NULL);              \
    if (err != VK_SUCCESS) {                                            \
      LOG_WARN("failed to bind " #buffer " to memory with error %s", ToString_VkResult(err)); \
    }                                                                   \
  }  while (0)
  BIND_BUFFER(cpu_memory, vertex_buffer, cpu_requirements[0], (void**)&drawer->pVertices);
  BIND_BUFFER(cpu_memory, transform_buffer, cpu_requirements[1], (void**)&drawer->pTransforms);
  BIND_BUFFER(cpu_memory, index_buffer, cpu_requirements[2], (void**)&drawer->pIndices);
  BIND_BUFFER(cpu_memory, storage_buffer, cpu_requirements[3], (void**)&drawer->pDraws);
  BIND_BUFFER(gpu_memory, indirect_buffer, gpu_requirements[0], NULL);
  BIND_BUFFER(gpu_memory, vertex_count_buffer, gpu_requirements[1], NULL);
#undef BIND_BUFFER

  // create descriptor set
  VkDescriptorSetLayoutBinding bindings[5];
  const uint32_t count = (drawer->enabled_KHR_draw_indirect_count) ? 5 : 4;
  for (uint32_t i = 0; i < count; i++)
    bindings[i] = (VkDescriptorSetLayoutBinding) {
      .binding = i,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
  err = AllocateDescriptorSets(bindings, count, &drawer->ds_set, 1, 0, "voxel/cull-set");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate descriptor set with error %s", ToString_VkResult(err));
    return err;
  }
  // update descriptor set
  VkWriteDescriptorSet write_sets[5];
  VkDescriptorBufferInfo buffer_infos[5];
  VkBuffer buffers[] = { drawer->storage_buffer, drawer->transform_buffer, drawer->indirect_buffer, drawer->vertex_count_buffer, drawer->indirect_buffer };
  for (size_t i = 0; i < count; i++) {
    buffer_infos[i] = (VkDescriptorBufferInfo) {
      .buffer = buffers[i],
      .offset = 0,
      .range  = VK_WHOLE_SIZE
    };
    write_sets[i] = (VkWriteDescriptorSet) {
      .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet          = drawer->ds_set,
      .dstBinding      = i,
      .descriptorCount = 1,
      .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo     = &buffer_infos[i]
    };
  }
  UpdateDescriptorSets(write_sets, count);

  return err;
}

INTERNAL void
NewFrameVoxel_Indirect(void* backend)
{
  Voxel_Backend_Indirect* drawer = backend;
  if ((g_window->frame_counter & 1) == 0) {
    drawer->transform_offset = 0;
    drawer->draw_offset = 0;
  }
  drawer->start_transform_offset = drawer->transform_offset;
  drawer->num_vertices = 0;
}

INTERNAL void
ClearCacheVoxel_Indirect(void* backend)
{
  Voxel_Backend_Indirect* drawer = backend;
  drawer->vertex_offset = 0;
}

INTERNAL void
RegenerateVoxel_Indirect(void* backend, Voxel_View* cached, Voxel_Grid* grid)
{
  Voxel_Backend_Indirect* drawer = backend;

  // skip this vertex if we're exceeding threshold...
  if (drawer->num_vertices >= VOXEL_VERTEX_THRESHOLD)
    return;

  grid->last_hash = grid->hash;
  uint32_t base_index = 0;
  VX_Draw_Data* draw = &drawer->pDraws[drawer->draw_offset++];
  CalculateVoxelGridSize(grid, &draw->half_size);
  draw->first_vertex = drawer->vertex_offset;
  // draw->instance_count = 1;
  draw->first_instance = drawer->transform_offset - drawer->start_transform_offset;
  draw->cull_mask = cached->cull_mask;
  grid->first_vertex = draw->first_vertex;
  for (size_t i = 0; i < 6; i++) {
    uint32_t index_offset = drawer->vertex_offset*3/2;
#if VX_USE_INDICES
    draw->vertex_count[i] = GenerateVoxelGridMeshGreedy(grid, drawer->pVertices + drawer->vertex_offset, i,
                                                        base_index, drawer->pIndices + index_offset);
    // draw->vertex_count[i] = GenerateVoxelGridMeshNaive(grid, drawer->pVertices + drawer->vertex_offset, i,
                                                       // base_index, drawer->pIndices + index_offset);
#else
    Assert(0 && "not implemented for vertices only ");
#endif
    base_index += draw->vertex_count[i];
    drawer->vertex_offset += draw->vertex_count[i];
    grid->offsets[i] = draw->vertex_count[i];
    drawer->num_vertices += draw->vertex_count[i];
  }
}

INTERNAL void
PushMeshVoxel_Indirect(void* backend, EID entity)
{
  Voxel_Backend_Indirect* drawer = backend;
  // add transform
  Transform* transform = GetComponent(Transform, entity);
  memcpy(&drawer->pTransforms[drawer->transform_offset],
         transform, sizeof(Transform));

  // try to use cache
  Voxel_View* cached = GetComponent(Voxel_View, entity);
  assert(cached);
  Voxel_Grid* grid = GetComponent(Voxel_Grid, cached->grid);
  assert(grid);
  if (grid->last_hash != grid->hash || grid->first_vertex > drawer->vertex_offset) {
    // regenerate if grid changed
    RegenerateVoxel_Indirect(drawer, cached, grid);
  } else {
    VX_Draw_Data* draw = &drawer->pDraws[drawer->draw_offset++];
    CalculateVoxelGridSize(grid, &draw->half_size);
    draw->first_vertex = grid->first_vertex;
    // draw->instance_count = 1;
    draw->first_instance = drawer->transform_offset - drawer->start_transform_offset;
    draw->cull_mask = cached->cull_mask;
    for (uint32_t i = 0; i < 6; i++) {
      draw->vertex_count[i] = grid->offsets[i];
    }
  }
  drawer->transform_offset++;
}

INTERNAL uint32_t
RenderVoxels_Indirect(void* backend, VkCommandBuffer cmd, const Camera* camera, uint32_t num_sets, VkDescriptorSet* sets, size_t num_draws)
{
  Voxel_Backend_Indirect* drawer = backend;

  if (drawer->num_vertices != 0) {
    LOG_DEBUG("submitted %u", (uint32_t)drawer->num_vertices);
  }

  Graphics_Pipeline* prog;
  // NOTE: we assume that if camera is perspective than it's shader uses normals
  if (camera->type == CAMERA_TYPE_PERSP) {
    prog = GetComponent(Graphics_Pipeline, g_voxel_pipeline_colored);
  } else {
    prog = GetComponent(Graphics_Pipeline, g_voxel_pipeline_shadow);
  }
  // bind buffers
  VkDeviceSize offsets[] = {
    0,
    drawer->start_transform_offset * sizeof(Transform),
    Log2_u32(camera->cull_mask) * num_draws * sizeof(VX_Vertex_Count)
  };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer, drawer->vertex_count_buffer };
  vkCmdBindVertexBuffers(cmd, 0, (camera->type == CAMERA_TYPE_PERSP) ? 3 : 2, buffers, offsets);
  vkCmdBindIndexBuffer(cmd, drawer->index_buffer, 0, VK_INDEX_TYPE_UINT32);

  // bind pipeline
  cmdBindGraphics(cmd, prog, num_sets, sets);

  if (!drawer->enabled_KHR_draw_indirect_count) {
    // submit draw commands
    uint32_t draw_calls = num_draws * 3;
    // HACK: I hate std140
    const uint32_t stride = 32;
    uint32_t offset = Log2_u32(camera->cull_mask) * draw_calls * stride;
    VkBuffer buffer = drawer->indirect_buffer;
    vkCmdDrawIndexedIndirect(cmd, buffer,
                             offset,
                             draw_calls,
                             stride);
  } else {
    uint32_t max_draw_calls = num_draws * 3;
    // HACK: I hate std140
    const uint32_t stride = 32;
    uint32_t offset = Log2_u32(camera->cull_mask) * max_draw_calls * stride;
    uint32_t count_offset = MAX_ACTIVE_CAMERAS * max_draw_calls * stride +
      Log2_u32(camera->cull_mask) * 16; // 16 stands for stride. I don't now why uints should be padded 16 but ok, fine, I'm not angry
    vkCmdDrawIndexedIndirectCountKHR(cmd,
                                     drawer->indirect_buffer, offset,
                                     drawer->indirect_buffer, count_offset,
                                     max_draw_calls, stride);
  }
  return 1;
}

INTERNAL void
CullPass_Indirect(void* backend, VkCommandBuffer cmd, const Camera* cameras, uint32_t num_cameras,
                  size_t num_draws)
{
  Voxel_Backend_Indirect* drawer = backend;
  Compute_Pipeline* prog = NULL;
  if (drawer->enabled_KHR_draw_indirect_count) {
    // fill draw counts with 0
    uint32_t dst_offset = MAX_ACTIVE_CAMERAS * num_draws * 3 * 32;
    const uint32_t uint_stride = 16;
    vkCmdFillBuffer(cmd, drawer->indirect_buffer, dst_offset, MAX_ACTIVE_CAMERAS * uint_stride, 0);
    cmdExecutionBarrier(cmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    struct {
      Mat4 projview_matrix;
      Vec3 camera_front;
      uint32_t cull_mask;
      Vec3 camera_position;
      uint32_t pass_id;
      uint32_t out_offset;
      uint32_t in_offset;
      uint32_t num_draws;
    } push_constant;

    EID last = ENTITY_NIL;
    for (uint32_t i = 0; i < num_cameras; i++) {
      EID pipeline_id = (cameras[i].type == CAMERA_TYPE_PERSP) ? g_voxel_pipeline_compute_ext_persp : g_voxel_pipeline_compute_ext_ortho;
      if (pipeline_id != last) {
        prog = GetComponent(Compute_Pipeline, pipeline_id);
        if (cameras[i].type == CAMERA_TYPE_PERSP) {
          VkDescriptorSet sets[2] = { drawer->ds_set, g_forward_pass->depth_pyramid.read_set };
          cmdBindCompute(cmd, prog, 2, sets);
        } else {
          cmdBindCompute(cmd, prog, 1, &drawer->ds_set);
        }
        last = pipeline_id;
      }
      memcpy(&push_constant.projview_matrix, &cameras[i].projview_matrix, sizeof(Mat4));
      push_constant.cull_mask = cameras[i].cull_mask;
      push_constant.camera_front = cameras[i].front;
      push_constant.camera_position = cameras[i].position;
      push_constant.pass_id = dst_offset / uint_stride + Log2_u32(cameras[i].cull_mask);
      push_constant.out_offset = Log2_u32(cameras[i].cull_mask) * num_draws;
      push_constant.in_offset = drawer->draw_offset - num_draws;
      push_constant.num_draws = num_draws;
      vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_COMPUTE_BIT,
                         0, sizeof(push_constant), &push_constant);
      vkCmdDispatch(cmd, (num_draws+63) / 64, 1, 1);
    }
  } else {
    struct {
      Mat4 projview_matrix;
      Vec3 camera_front;
      uint32_t cull_mask;
      Vec3 camera_position;
      uint32_t out_offset;
      uint32_t in_offset;
      uint32_t num_draws;
    } push_constant;
    EID last = ENTITY_NIL;
    for (uint32_t i = 0; i < num_cameras; i++) {
      EID pipeline_id = (cameras[i].type == CAMERA_TYPE_PERSP) ? g_voxel_pipeline_compute_persp : g_voxel_pipeline_compute_ortho;
      if (pipeline_id != last) {
        prog = GetComponent(Compute_Pipeline, pipeline_id);
        if (cameras[i].type == CAMERA_TYPE_PERSP) {
          VkDescriptorSet sets[2] = { drawer->ds_set, g_forward_pass->depth_pyramid.read_set };
          cmdBindCompute(cmd, prog, 2, sets);
        } else {
          cmdBindCompute(cmd, prog, 1, &drawer->ds_set);
        }
        last = pipeline_id;
      }
      memcpy(&push_constant.projview_matrix, &cameras[i].projview_matrix, sizeof(Mat4));
      push_constant.cull_mask = cameras[i].cull_mask;
      push_constant.camera_front = cameras[i].front;
      push_constant.camera_position = cameras[i].position;
      push_constant.out_offset = Log2_u32(cameras[i].cull_mask) * num_draws;
      push_constant.in_offset = drawer->draw_offset - num_draws;
      push_constant.num_draws = num_draws;
      vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_COMPUTE_BIT,
                         0, sizeof(push_constant), &push_constant);
      vkCmdDispatch(cmd, (num_draws+63) / 64, 1, 1);
    }
  }
  cmdExecutionBarrier(cmd,
                      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT);
}

INTERNAL void
DestroyVoxel_Indirect(void* backend, Deletion_Queue* dq)
{
  Voxel_Backend_Indirect* drawer = backend;
  if (dq == NULL) {
    vkDestroyBuffer(g_device->logical_device, drawer->vertex_count_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->indirect_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->storage_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->index_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->transform_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->vertex_buffer, NULL);
  } else {
    AddForDeletion(dq, (uint64_t)drawer->vertex_count_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->indirect_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->storage_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->index_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->transform_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->vertex_buffer, VK_OBJECT_TYPE_BUFFER);
  }
}

INTERNAL uint32_t
VoxelStatistics_Indirect(void* backend, char* buff)
{
  Voxel_Backend_Indirect* drawer = backend;
  uint32_t count = 0;
  count += stbsp_sprintf(buff + count, "[vertices: %u] ", (uint32_t)drawer->vertex_offset);
  count += stbsp_sprintf(buff + count, "[draws: %u] ",    (uint32_t)drawer->draw_offset);
  return count;
}


/// Voxel drawer

INTERNAL VkResult
SetVoxelBackend_Slow(Voxel_Drawer* drawer, Deletion_Queue* dq)
{
  // HACK: this works for now, but we'd want to do something better
  FOREACH_COMPONENT(Voxel_Grid) {
    components[i].first_vertex = UINT32_MAX;
  }

  if (dq) {
    drawer->clear_cache_func(&drawer->backend);
    drawer->destroy_func(&drawer->backend, dq);
  }
  ResetVideoMemory(&drawer->cpu_memory);
  ResetVideoMemory(&drawer->gpu_memory);
  VkResult err = CreateVoxelBackend_Slow(&drawer->backend, &drawer->cpu_memory, drawer->max_vertices, drawer->max_draws);
  if (err != VK_SUCCESS)
    return err;

  drawer->new_frame_func       = NewFrameVoxel_Slow;
  drawer->clear_cache_func     = ClearCacheVoxel_Slow;
  drawer->regenerate_mesh_func = RegenerateVoxel_Slow;
  drawer->push_mesh_func       = PushMeshVoxel_Slow;
  drawer->render_voxels_func   = RenderVoxels_Slow;
  drawer->cull_pass_func       = CullPass_Slow;
  drawer->destroy_func         = DestroyVoxel_Slow;
  drawer->stat_func            = VoxelStatistics_Slow;

  LOG_INFO("classic voxel drawing backend set");
  return err;
}

INTERNAL VkResult
SetVoxelBackend_Indirect(Voxel_Drawer* drawer, Deletion_Queue* dq)
{
  // HACK: this works for now, but we'd want to do something better
  FOREACH_COMPONENT(Voxel_Grid) {
    components[i].first_vertex = UINT32_MAX;
  }

  if (dq) {
    drawer->clear_cache_func(&drawer->backend);
    drawer->destroy_func(&drawer->backend, dq);
  }
  ResetVideoMemory(&drawer->cpu_memory);
  ResetVideoMemory(&drawer->gpu_memory);
  VkResult err = CreateVoxelBackend_Indirect(&drawer->backend, &drawer->cpu_memory, &drawer->gpu_memory, drawer->max_vertices, drawer->max_draws);
  if (err != VK_SUCCESS)
    return err;

  drawer->new_frame_func = NewFrameVoxel_Indirect;
  drawer->clear_cache_func = ClearCacheVoxel_Indirect;
  drawer->regenerate_mesh_func = RegenerateVoxel_Indirect;
  drawer->push_mesh_func = PushMeshVoxel_Indirect;
  drawer->render_voxels_func = RenderVoxels_Indirect;
  drawer->cull_pass_func = CullPass_Indirect;
  drawer->destroy_func = DestroyVoxel_Indirect;
  drawer->stat_func = VoxelStatistics_Indirect;

  LOG_INFO("indirect voxel drawing backend set");
  return err;
}

INTERNAL VkResult
CreateVoxelDrawer(Voxel_Drawer* drawer, uint32_t max_vertices, uint32_t max_draws)
{
  PROFILE_FUNCTION();
  drawer->max_draws = max_draws;
  drawer->max_vertices = max_vertices;
#if VX_USE_INDICES
  drawer->max_indices = max_vertices * 3 / 2;
#endif
  drawer->cpu_memory.handle = VK_NULL_HANDLE;
  drawer->gpu_memory.handle = VK_NULL_HANDLE;

  VkResult err;
  // use fast backend if possible
  if (g_device->features.multiDrawIndirect) {
    err = SetVoxelBackend_Indirect(drawer, NULL);
  } else {
    err = SetVoxelBackend_Slow(drawer, NULL);
  }
    // err = SetVoxelBackend_Slow(drawer, NULL);

  if (g_device->features.pipelineStatisticsQuery) {
    CreatePipelineStats(&drawer->pipeline_stats_fragment);
    CreatePipelineStats(&drawer->pipeline_stats_shadow);
  } else {
    memset(&drawer->pipeline_stats_fragment, 0, sizeof(Pipeline_Stats));
    memset(&drawer->pipeline_stats_shadow,   0, sizeof(Pipeline_Stats));
  }

  g_voxel_pipeline_colored           = CreateEntity(g_ecs);
  g_voxel_pipeline_shadow            = CreateEntity(g_ecs);
  g_voxel_pipeline_compute_ortho     = CreateEntity(g_ecs);
  g_voxel_pipeline_compute_persp     = CreateEntity(g_ecs);
  g_voxel_pipeline_compute_ext_ortho = CreateEntity(g_ecs);
  g_voxel_pipeline_compute_ext_persp = CreateEntity(g_ecs);

  return err;
}

INTERNAL void
DestroyVoxelDrawer(Voxel_Drawer* drawer)
{
  for (int i = 0; i < 2; i++) {
    vkDestroyQueryPool(g_device->logical_device, drawer->pipeline_stats_fragment.query_pools[i], NULL);
    vkDestroyQueryPool(g_device->logical_device, drawer->pipeline_stats_shadow.query_pools[i],   NULL);
  }
  drawer->destroy_func(&drawer->backend, NULL);
  FreeVideoMemory(&drawer->gpu_memory);
  FreeVideoMemory(&drawer->cpu_memory);
}

INTERNAL void
NewVoxelDrawerFrame(Voxel_Drawer* drawer)
{
  drawer->new_frame_func(&drawer->backend);
  drawer->num_draws = 0;
  // get query results
  if (g_window->frame_counter > 1) {
    GetPipelineStats(&drawer->pipeline_stats_fragment);
    GetPipelineStats(&drawer->pipeline_stats_shadow);
  }
}

INTERNAL void
ClearVoxelDrawerCache(Voxel_Drawer* drawer)
{
  drawer->clear_cache_func(&drawer->backend);
}

INTERNAL void
PushMeshToVoxelDrawer(Voxel_Drawer* drawer, EID entity)
{
  drawer->push_mesh_func(&drawer->backend, entity);
  drawer->num_draws++;
}

INTERNAL uint32_t
DrawVoxels(Voxel_Drawer* drawer, VkCommandBuffer cmd, const Camera* mesh_pass,
           uint32_t num_sets, VkDescriptorSet* sets)
{
  return drawer->render_voxels_func(&drawer->backend, cmd, mesh_pass,
                                    num_sets, sets, drawer->num_draws);
}

INTERNAL uint32_t
VoxelDrawerStatistics(Voxel_Drawer* drawer, char* buff)
{
  LOG_INFO("main pass:");
  PrintPipelineStats(&drawer->pipeline_stats_fragment, "--");
  LOG_INFO("shadow pass:");
  PrintPipelineStats(&drawer->pipeline_stats_shadow, "--");
  return drawer->stat_func(&drawer->backend, buff);
}

INTERNAL void
PipelineVoxelVertices1(const VkVertexInputAttributeDescription** attributes, uint32_t* num_attributes,
                       const VkVertexInputBindingDescription** bindings, uint32_t* num_bindings,
                       int using_colors)
{
  GLOBAL VkVertexInputBindingDescription g_bindings[] = {
    { 0, sizeof(Vertex_X3C), VK_VERTEX_INPUT_RATE_VERTEX },
    { 1, sizeof(Transform), VK_VERTEX_INPUT_RATE_INSTANCE }
  };
  GLOBAL VkVertexInputAttributeDescription g_attributes1[] = {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex_X3C, position) },
    { 1, 0, VK_FORMAT_R32_UINT, offsetof(Vertex_X3C, color) },
    { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Transform, rotation) },
    { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Transform, position) },
    { 4, 1, VK_FORMAT_R32_SFLOAT, offsetof(Transform, scale) }
  };
  GLOBAL VkVertexInputAttributeDescription g_attributes2[4] = {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex_X3C, position) },
    { 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Transform, rotation) },
    { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Transform, position) },
    { 3, 1, VK_FORMAT_R32_SFLOAT, offsetof(Transform, scale) },
  };
  if (using_colors) {
    *attributes = g_attributes1;
    *num_attributes = ARR_SIZE(g_attributes1);
  } else {
    *attributes = g_attributes2;
    *num_attributes = ARR_SIZE(g_attributes2);
  }
  *bindings = g_bindings;
  *num_bindings = ARR_SIZE(g_bindings);
}

INTERNAL void
PipelineVoxelVertices2(const VkVertexInputAttributeDescription** attributes, uint32_t* num_attributes,
                       const VkVertexInputBindingDescription** bindings, uint32_t* num_bindings,
                       int using_colors)
{
  GLOBAL VkVertexInputBindingDescription g_bindings[] = {
    { 0, sizeof(Vertex_X3C), VK_VERTEX_INPUT_RATE_VERTEX },
    { 1, sizeof(Transform), VK_VERTEX_INPUT_RATE_INSTANCE },
    { 2, sizeof(VX_Vertex_Count), VK_VERTEX_INPUT_RATE_INSTANCE }
  };
  GLOBAL VkVertexInputAttributeDescription g_attributes1[] = {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex_X3C, position) },
    { 1, 0, VK_FORMAT_R32_UINT, offsetof(Vertex_X3C, color) },
    { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Transform, rotation) },
    { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Transform, position) },
    { 4, 1, VK_FORMAT_R32_SFLOAT, offsetof(Transform, scale) },
    { 5, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count0) },
    { 6, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count1) },
    { 7, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count2) },
    { 8, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count3) },
    { 9, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count4) },
    { 10, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, debug_data1) },
    { 11, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, debug_data2) },
    { 12, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, debug_data3) },
  };
  GLOBAL VkVertexInputAttributeDescription g_attributes2[4] = {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex_X3C, position) },
    { 1, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Transform, rotation) },
    { 2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Transform, position) },
    { 3, 1, VK_FORMAT_R32_SFLOAT, offsetof(Transform, scale) },
  };
  if (using_colors) {
    *attributes = g_attributes1;
    // NOTE: we assume that if using_colors than we're using normals also
    // TODO: provide a way to use colors but not normals
    *num_attributes = ARR_SIZE(g_attributes1);
  } else {
    *attributes = g_attributes2;
    *num_attributes = ARR_SIZE(g_attributes2);
  }
  *bindings = g_bindings;
  *num_bindings = (using_colors ? 3 : 2);
}

// calculate oriented bounding box's corners
INTERNAL void
CalculateVoxelGridOBB(const Voxel_Grid* grid, const Transform* transform, OBB* obb)
{
  Vec3 half_size;
  CalculateVoxelGridSize(grid, &half_size);
  CalculateObjectOBB(&half_size, transform, obb);
}

INTERNAL void
CullPass(VkCommandBuffer cmd, Voxel_Drawer* drawer, const Camera* mesh_passes, uint32_t num_passes)
{
  drawer->cull_pass_func(&drawer->backend, cmd, mesh_passes, num_passes, drawer->num_draws);
}

INTERNAL void
CreateVoxelPipelineClassic(Pipeline_Desc* description)
{
  static VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  static VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_NONE,
    // .cullMode = VK_CULL_MODE_FRONT_BIT,
    .depth_bias_enable = VK_FALSE,
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 1,
    .attachments = &colorblend_attachment,
    .dynamic_state_count = ARR_SIZE(dynamic_states),
    .dynamic_states = dynamic_states,
    .render_pass = g_forward_pass->render_pass,
    .subpass = 0,
    .marker = "forward/voxel-pipeline"
  };
  PipelineVoxelVertices1(&description->vertex_attributes, &description->vertex_attribute_count,
                        &description->vertex_bindings, &description->vertex_binding_count,
                        1);
}

INTERNAL void
CreateVoxelPipelineIndirect(Pipeline_Desc* description)
{
  static VkPipelineColorBlendAttachmentState colorblend_attachment = {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT,
  };
  static VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    // .polygonMode = VK_POLYGON_MODE_LINE,
    .cullMode = VK_CULL_MODE_NONE,
    // .cullMode = VK_CULL_MODE_FRONT_BIT,
    .depth_bias_enable = VK_FALSE,
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 1,
    .attachments = &colorblend_attachment,
    .dynamic_state_count = ARR_SIZE(dynamic_states),
    .dynamic_states = dynamic_states,
    .render_pass = g_forward_pass->render_pass,
    .subpass = 0,
    .marker = "forward/voxel-pipeline"
  };
  PipelineVoxelVertices2(&description->vertex_attributes, &description->vertex_attribute_count,
                        &description->vertex_bindings, &description->vertex_binding_count,
                        1);
}

INTERNAL void
CreateVoxelPipelineShadow(Pipeline_Desc* description)
{
  // NOTE: use depth bias < 0 because our depth is inverted
  static VkDynamicState dynamic_state = VK_DYNAMIC_STATE_DEPTH_BIAS;
  *description = (Pipeline_Desc) {
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_FRONT_BIT,
    .depth_bias_enable = VK_TRUE,
    .depth_test = VK_TRUE,
    .depth_write = VK_TRUE,
    .depth_compare_op = VK_COMPARE_OP_GREATER,
    .blend_logic_enable = VK_FALSE,
    .attachment_count = 0,
    .dynamic_state_count = 1,
    .dynamic_states = &dynamic_state,
    .render_pass = g_shadow_pass->render_pass,
    .subpass = 0,
    .marker = "voxels-to-shadow-map",
  };
  PipelineVoxelVertices1(&description->vertex_attributes, &description->vertex_attribute_count,
                        &description->vertex_bindings, &description->vertex_binding_count,
                        0);
  ShadowPassViewport(g_shadow_pass, &description->viewport, &description->scissor);
}


/// Voxel generation (procedural or not)

INTERNAL void
GenerateVoxelSphere(Voxel_Grid* grid, int radius, Voxel fill)
{
  if ((int)grid->width != radius*2+1 ||
      (int)grid->depth != radius*2+1 ||
      (int)grid->height != radius*2+1) {
    LOG_WARN("radius doesn't match grid's extents");
    return;
  }
  for (int z = 0; z < radius*2+1; z++)
    for (int y = 0; y < radius*2+1; y++)
      for (int x = 0; x < radius*2+1; x++) {
        int xr = abs(x-radius);
        int yr = abs(y-radius);
        int zr = abs(z-radius);
        if (xr*xr + yr*yr + zr*zr <= radius*radius) {
          GetInVoxelGrid(grid, x, y, z) = fill;
        }
      }
}

INTERNAL void
FillVoxelGrid(Voxel_Grid* grid, Voxel fill)
{
#if VX_USE_BLOCKS
  // NOTE: this may be wrong as padding in blocks get filled too
  // this would produce incorrect hashes. But I literally don't care😎
  memset(grid->data->ptr, fill, VoxelGridBytes(grid));
#else
  memset(grid->data->ptr, fill, VoxelGridBytes(grid));
#endif
}

/**
   This should not be used.
   I made this for debugging voxel blocks.
 */
INTERNAL void
DebugDrawVoxelBlocks(Debug_Drawer* debug_drawer, const Voxel_Grid* grid, const OBB* obb)
{
  const uint32_t indices[24] = {
    0, 1,
    1, 3,
    3, 2,
    2, 0,

    4, 5,
    5, 7,
    7, 6,
    6, 4,

    0, 4,
    1, 5,
    2, 6,
    3, 7
  };
  const uint32_t colors[] = {
    PACK_COLOR(255, 255, 0, 255),
    PACK_COLOR(0, 255, 0, 255),
  };
  float sx = 4.0f / grid->width;
  float sy = 4.0f / grid->height;
  float sz = 4.0f / grid->depth;
  for (size_t i = 0; i < ARR_SIZE(indices); i += 2) {
    Vec3 a = obb->corners[indices[i]];
    Vec3 d = VEC3_SUB(obb->corners[indices[i+1]], obb->corners[indices[i]]);
    d.x *= sx;
    d.y *= sy;
    d.z *= sz;
    for (uint32_t j = 0; j < grid->width/4; j++) {
      Vec3 b = VEC3_ADD(a, d);
      AddDebugLine(debug_drawer,
                   &a, &b,
                   colors[j&1]);
      a = b;
    }
  }
}
