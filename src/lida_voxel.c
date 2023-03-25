/*
  lida_voxel.c

  Voxel loading, creation, rendering etc.

 */

typedef uint8_t Voxel;
#define VX_USE_INDICES 1
#define VX_USE_CULLING 1
#define MAX_MESH_PASSES 8

// stores voxels as plain 3D array
typedef struct {

  Allocation* data;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint64_t hash;
  uint32_t palette[256];

} Voxel_Grid;
DECLARE_COMPONENT(Voxel_Grid);

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
  char padding[12];

} VX_Vertex_Count;

typedef struct {

  // this is for vkCmdDraw
  uint32_t vertexCount;
  uint32_t firstVertex;
  uint32_t firstInstance;
  uint32_t instanceCount;

} VX_Draw_Command;

enum {
  MESH_PASS_ORTHO = 0,
  MESH_PASS_PERSP,
  MESH_PASS_USE_NORMALS = 2,
};

typedef struct {

  Mat4 projview_matrix;
  Vec3 camera_dir;
  uint32_t cull_mask;
  Vec3 camera_pos;
  int flags;

} Mesh_Pass;
DECLARE_COMPONENT(Mesh_Pass);

typedef struct {

  uint64_t hash;
  uint32_t first_vertex;
  uint32_t offsets[6];
  int cull_mask;

} Voxel_Cached;
DECLARE_COMPONENT(Voxel_Cached);

typedef struct {

  VkBuffer vertex_buffer;
  VkBuffer transform_buffer;
  VkBuffer index_buffer;

  Vertex_X3C* pVertices;
  Transform* pTransforms;
  uint32_t* pIndices;

  // reset each frame
  size_t vertex_offset;
  size_t transform_offset;
  size_t start_transform_offset;
  Allocation* draws;
  size_t num_draws;

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

  void (*new_frame_func)(void* backend);
  void (*clear_cache_func)(void* backend);
  void (*regenerate_mesh_func)(void* backend, Voxel_Cached* cached, const Voxel_Grid* grid);
  void (*push_mesh_func)(void* backend, ECS* ecs, EID entity);
  uint32_t (*render_voxels_func)(void* backend, VkCommandBuffer cmd, const Mesh_Pass* mesh_pass, uint32_t num_sets, VkDescriptorSet* sets, size_t num_draws);
  void (*cull_pass_func)(void* backend, VkCommandBuffer cmd, const Mesh_Pass* mesh_passes, uint32_t num_passes, size_t num_draws);
  void (*destroy_func)(void* backend, Deletion_Queue* dq);

} Voxel_Drawer;

Voxel_Drawer* g_vox_drawer;

// NOTE: this doesn't do bounds checking
// NOTE: setting a voxel value with this macro is unsafe, hash won't be correct,
// consider using SetInVoxelGrid
#define GetInVoxelGrid(grid, x, y, z) ((Voxel*)(grid)->data->ptr)[(x) + (y)*(grid)->width + (z)*(grid)->width*(grid)->height]

Allocator* g_vox_allocator;

EID g_voxel_pipeline_classic;
EID g_voxel_pipeline_indirect;
EID g_voxel_pipeline_shadow;
EID g_voxel_pipeline_compute;
EID g_voxel_pipeline_compute_ext_ortho;
EID g_voxel_pipeline_compute_ext_persp;

// TODO: compress voxels on disk using RLE(Run length Encoding)


/// Voxel grid

INTERNAL int
ReallocateVoxelGrid(Allocator* allocator, Voxel_Grid* grid, uint32_t w, uint32_t h, uint32_t d)
{
  Allocation* old_data = grid->data;
  grid->data = DoAllocation(allocator, w*h*d, "voxel-grid");
  if (grid->data == NULL) {
    grid->data = old_data;
    LOG_WARN("out of memory");
    return -1;
  }
  if (old_data) {
    for (uint32_t i = 0; i < grid->depth; i++) {
      for (uint32_t j = 0; j < grid->height; j++) {
        memcpy((Voxel*)grid->data->ptr + i*w*h + j*w,
               (Voxel*)old_data->ptr + i*grid->width*grid->height + j*grid->width,
               grid->width);
      }
    }
    FreeAllocation(allocator, old_data);
  } else {
    memset(grid->data->ptr, 0, w*h*d);
  }
  grid->width = w;
  grid->height = h;
  grid->depth = d;
  return 0;
}

INTERNAL int
AllocateVoxelGrid(Allocator* allocator, Voxel_Grid* grid, uint32_t w, uint32_t h, uint32_t d)
{
  grid->data = NULL;
  return ReallocateVoxelGrid(allocator, grid, w, h, d);
}

INTERNAL void
FreeVoxelGrid(Allocator* allocator, Voxel_Grid* grid)
{
  if (grid->data) {
    FreeAllocation(allocator, grid->data);
    grid->data = NULL;
  }
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
  // TODO: my dream is to make this function execute fast, processing
  // 4 or 8 voxels at same time
  Vertex_X3C* const first_vertex = vertices;
  Vec3 half_size;
  float inv_size = CalculateVoxelGridSize(grid, &half_size);
  const uint32_t dims[3] = { grid->width, grid->height, grid->depth };
  const int d = face >> 1;
  const int u = (d+1)%3, v = (d+2)%3;
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
#if 0
        // grow quad while all voxels in that quad are the same
        while (pos[v] < dims[v]) {
          pos[u] = i;
          if (GetInVoxelGrid(grid, pos[0], pos[1], pos[2]) != start_voxel)
            break;
          pos[u]++;
          while (pos[u] < min_i &&
                 GetInVoxelGrid(grid, pos[0], pos[1], pos[2]) == start_voxel) {
            pos[u]++;
        int p[3] = { 0 };
        // check if at least 1 voxel is visible. FIXME: I think when
        // this approach generates the most perfect meshes, it
        // generates ugly meshes: when camera is inside a mesh some
        // unnecessary voxels are seen.
        for (p[v] = start_pos[v], p[d] = layer; (uint32_t)p[v] < start_pos[v] + offset[v]; p[v]++) {
          for (p[u] = start_pos[u]; (uint32_t)p[u] < start_pos[u] + offset[u]; p[u]++) {
            Voxel near_voxel = GetInVoxelGridChecked(grid,
                                                     p[0] + vox_normals[face].x,
                                                     p[1] + vox_normals[face].y,
                                                     p[2] + vox_normals[face].z);
            if (near_voxel == 0) goto process;
          }
        }
          }
          if (pos[u] < min_i) min_i = pos[u];
          pos[v]++;
        }
        uint32_t offset[3] = { 0 };
        offset[u] = min_i - start_pos[u]; // width of quad
        offset[v] = pos[v] - start_pos[v]; // height of quad
        int p[3] = { 0 };
        // check if at least 1 voxel is visible. FIXME: I think when
        // this approach generates the most perfect meshes, it
        // generates ugly meshes: when camera is inside a mesh some
        // unnecessary voxels are seen.
        for (p[v] = start_pos[v], p[d] = layer; (uint32_t)p[v] < start_pos[v] + offset[v]; p[v]++) {
          for (p[u] = start_pos[u]; (uint32_t)p[u] < start_pos[u] + offset[u]; p[u]++) {
            Voxel near_voxel = GetInVoxelGridChecked(grid,
                                                     p[0] + vox_normals[face].x,
                                                     p[1] + vox_normals[face].y,
                                                     p[2] + vox_normals[face].z);
            if (near_voxel == 0) goto process;
          }
        }
        continue;
      process:
#else
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
#endif
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
  LOG_DEBUG("wrote %u vertices", (uint32_t)(vertices - first_vertex));
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
  if (AllocateVoxelGrid(allocator, grid, model->size_x, model->size_y, model->size_z)) {
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
  grid->hash = HashMemory64(grid->data->ptr, grid->width * grid->height * grid->depth);
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
#if VX_USE_INDICES
  CREATE_BUFFER(index_buffer, max_vertices * 3 / 2 * sizeof(uint32_t),
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "voxel-draw/index-buffer");
#endif
#undef CREATE_BUFFER

  // allocate memory for buffers
#if VX_USE_INDICES
  VkMemoryRequirements buffer_requirements[3];
#else
  VkMemoryRequirements buffer_requirements[2];
#endif
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_buffer, &buffer_requirements[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->transform_buffer, &buffer_requirements[1]);
#if VX_USE_INDICES
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->index_buffer, &buffer_requirements[2]);
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
#if VX_USE_INDICES
  BIND_BUFFER(cpu_memory, index_buffer, buffer_requirements[2], (void**)&drawer->pIndices);
#endif
#undef BIND_BUFFER
  return err;
}

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
}

INTERNAL void
ClearCacheVoxel_Slow(void* backend)
{
  Voxel_Backend_Slow* drawer = backend;
  drawer->vertex_offset = 0;
}

INTERNAL void
RegenerateVoxel_Slow(void* backend, Voxel_Cached* cached, const Voxel_Grid* grid)
{
  Voxel_Backend_Slow* drawer = backend;
  cached->hash = grid->hash;

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
      cached->first_vertex = command->firstVertex;
    }
    command->firstInstance = drawer->transform_offset - drawer->start_transform_offset;
    command->instanceCount = 1;
    drawer->vertex_offset += command->vertexCount;
    cached->offsets[i] = command->vertexCount;
  }
}

INTERNAL void
PushMeshVoxel_Slow(void* backend, ECS* ecs, EID entity)
{
  Voxel_Backend_Slow* drawer = backend;
  uint32_t index = drawer->num_meshes;
  // add transform
  Transform* transform = GetComponent(Transform, entity);
  memcpy(&drawer->pTransforms[drawer->transform_offset],
         transform, sizeof(Transform));
  EID* meshes = drawer->meshes->ptr;
  Voxel_Grid* grid = GetComponent(Voxel_Grid, entity);
  VX_Draw_Command* draws = drawer->draws->ptr;

  // try to use instancing if same mesh is pushed twice
  if (index > 0) {
    Voxel_Cached* prev = GetComponent(Voxel_Cached, meshes[index-1]);
    if (prev->hash == grid->hash) {
      for (uint32_t i = 0; i < 6; i++) {
        draws[drawer->num_draws-6+i].instanceCount++;
      }
      goto end;
    }
  }

  // try to use cache
  Voxel_Cached* cached = GetComponent(Voxel_Cached, entity);
  if (cached) {
    if (cached->hash != grid->hash || cached->first_vertex > drawer->vertex_offset) {
      // regenerate if grid changed
      RegenerateVoxel_Slow(drawer, cached, grid);
      goto end;
    }
    uint32_t vertex_offset = cached->first_vertex;
    for (uint32_t i = 0; i < 6; i++) {
      VX_Draw_Command* command = &draws[drawer->num_draws++];
      command->firstVertex = vertex_offset;
      command->vertexCount = cached->offsets[i];
      command->firstInstance = drawer->transform_offset - drawer->start_transform_offset;
      command->instanceCount = 1;
      vertex_offset += cached->offsets[i];
    }
  } else {
    // didn't succeed, generate new vertices
    cached = AddComponent(ecs, Voxel_Cached, entity);
    RegenerateVoxel_Slow(drawer, cached, grid);
  }
 end:
  drawer->transform_offset++;
  meshes[drawer->num_meshes++] = entity;
}

INTERNAL uint32_t
RenderVoxels_Slow(void* backend, VkCommandBuffer cmd, const Mesh_Pass* mesh_pass, uint32_t num_sets, VkDescriptorSet* sets, size_t num_draws)
{
  Voxel_Backend_Slow* drawer = backend;
  (void)num_draws; // FIXME: maybe we should get rid of num_draws argument
  Graphics_Pipeline* prog;
  if (mesh_pass->flags & MESH_PASS_USE_NORMALS) {
    prog = GetComponent(Graphics_Pipeline, g_voxel_pipeline_classic);
  } else {
    prog = GetComponent(Graphics_Pipeline, g_voxel_pipeline_classic);
  }
  // bind vertices
  VkDeviceSize offsets[] = { 0, drawer->start_transform_offset * sizeof(Transform) };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer };
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
  if (mesh_pass->flags & MESH_PASS_USE_NORMALS) {
    for (uint32_t normal_id = 0; normal_id < 6; normal_id++) {
      // pass normal ID
      vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_VERTEX_BIT,
                         0, sizeof(uint32_t), &normal_id);
      for (uint32_t i = normal_id; i < drawer->num_draws; i += 6) {
        VX_Draw_Command* command = &draws[i];
#if VX_USE_CULLING
        EID mesh = meshes[i/6];
        Voxel_Cached* cached = GetComponent(Voxel_Cached, mesh);
        if ((cached->cull_mask & mesh_pass->cull_mask) == 0) {
          continue;
        }
        // TODO(render): find a way to cull instanced objects.
        if (command->instanceCount == 1) {
          Transform* transform = GetComponent(Transform, mesh);
          // try to backface cull this face
          Vec3 dist = VEC3_SUB(transform->position, mesh_pass->camera_pos);
          Vec3 normal = f_vox_normals[normal_id];
          RotateByQuat(&normal, &transform->rotation, &normal);
          float dot = VEC3_DOT(dist, normal);
          if (dot > 0)
            continue;
        }
#endif

#if VX_USE_INDICES
        uint32_t vertex_offset = draws[i - i%6].firstVertex;
        vkCmdDrawIndexed(cmd, command->vertexCount*3/2, command->instanceCount,
                         command->firstVertex*3/2, vertex_offset, command->firstInstance);
#else
        vkCmdDraw(cmd,
                  command->vertexCount, command->instanceCount,
                  command->firstVertex, command->firstInstance);
#endif
        draw_calls++;
      }
    }
  } else {
    for (uint32_t i = 0; i < drawer->num_draws; i += 6) {
#if VX_USE_CULLING
      Voxel_Cached* cached = GetComponent(Voxel_Cached, meshes[i/6]);
      if ((cached->cull_mask & mesh_pass->cull_mask) == 0) {
        continue;
      }
#endif
      VX_Draw_Command* command = &draws[i];
      // merge draw calls
#if VX_USE_INDICES
      uint32_t index_count = 0;
      for (uint32_t j = 0; j < 6; j++) {
        index_count += command[j].vertexCount * 3 / 2;
      }
      uint32_t vertex_offset = command->firstVertex;
      vkCmdDrawIndexed(cmd, index_count, command->instanceCount,
                       command->firstVertex*3/2, vertex_offset, command->firstInstance);
#else
      uint32_t vertex_count = 0;
      for (uint32_t j = 0; j < 6; j++) {
        vertex_count += command[j].vertexCount;
      }
      vkCmdDraw(cmd,
                vertex_count, command->instanceCount,
                command->firstVertex, command->firstInstance);
#endif
      draw_calls++;
    }
  }
  return draw_calls;
}

INTERNAL void
CullPass_Slow(void* backend, VkCommandBuffer cmd, const Mesh_Pass* mesh_passes, uint32_t num_passes,
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
    vkDestroyBuffer(g_device->logical_device, drawer->index_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->transform_buffer, NULL);
    vkDestroyBuffer(g_device->logical_device, drawer->vertex_buffer, NULL);
  } else {
    AddForDeletion(dq, (uint64_t)drawer->index_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->transform_buffer, VK_OBJECT_TYPE_BUFFER);
    AddForDeletion(dq, (uint64_t)drawer->vertex_buffer, VK_OBJECT_TYPE_BUFFER);
  }
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
  // NOTE: 5 is max number of faces produced by a single model
  CREATE_BUFFER(indirect_buffer, MAX_MESH_PASSES * 5 * max_draws * 32/*sizeof(VkDrawIndexedIndirectCommand)*/,
                indirect_flags, "voxel-drawer/indirect-buffer");
  CREATE_BUFFER(vertex_count_buffer, MAX_MESH_PASSES * max_draws * sizeof(VX_Vertex_Count),
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
      .range = VK_WHOLE_SIZE
    };
    write_sets[i] = (VkWriteDescriptorSet) {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = drawer->ds_set,
      .dstBinding = i,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &buffer_infos[i]
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
}

INTERNAL void
ClearCacheVoxel_Indirect(void* backend)
{
  Voxel_Backend_Indirect* drawer = backend;
  drawer->vertex_offset = 0;
}

INTERNAL void
RegenerateVoxel_Indirect(void* backend, Voxel_Cached* cached, const Voxel_Grid* grid)
{
  Voxel_Backend_Indirect* drawer = backend;
  cached->hash = grid->hash;
  uint32_t base_index = 0;
  VX_Draw_Data* draw = &drawer->pDraws[drawer->draw_offset++];
  CalculateVoxelGridSize(grid, &draw->half_size);
  draw->first_vertex = drawer->vertex_offset;
  // draw->instance_count = 1;
  draw->first_instance = drawer->transform_offset - drawer->start_transform_offset;
  draw->cull_mask = cached->cull_mask;
  cached->first_vertex = draw->first_vertex;
  for (size_t i = 0; i < 6; i++) {
    uint32_t index_offset = drawer->vertex_offset*3/2;
    draw->vertex_count[i] = GenerateVoxelGridMeshGreedy(grid, drawer->pVertices + drawer->vertex_offset, i,
                                                       base_index, drawer->pIndices + index_offset);
    base_index += draw->vertex_count[i];
    drawer->vertex_offset += draw->vertex_count[i];
    cached->offsets[i] = draw->vertex_count[i];
  }
}

INTERNAL void
PushMeshVoxel_Indirect(void* backend, ECS* ecs, EID entity)
{
  Voxel_Backend_Indirect* drawer = backend;
  // add transform
  Transform* transform = GetComponent(Transform, entity);
  memcpy(&drawer->pTransforms[drawer->transform_offset],
         transform, sizeof(Transform));
  Voxel_Grid* grid = GetComponent(Voxel_Grid, entity);

  // try to use cache
  Voxel_Cached* cached = GetComponent(Voxel_Cached, entity);
  if (cached) {
    if (cached->hash != grid->hash || cached->first_vertex > drawer->vertex_offset) {
      // regenerate if grid changed
      RegenerateVoxel_Indirect(drawer, cached, grid);
      goto end;
    }
    VX_Draw_Data* draw = &drawer->pDraws[drawer->draw_offset++];
    CalculateVoxelGridSize(grid, &draw->half_size);
    draw->first_vertex = cached->first_vertex;
    // draw->instance_count = 1;
    draw->first_instance = drawer->transform_offset - drawer->start_transform_offset;
    draw->cull_mask = cached->cull_mask;
    for (uint32_t i = 0; i < 6; i++) {
      draw->vertex_count[i] = cached->offsets[i];
    }
  } else {
    // didn't succeed, generate new vertices
    cached = AddComponent(ecs, Voxel_Cached, entity);
    RegenerateVoxel_Indirect(drawer, cached, grid);
  }
 end:
  drawer->transform_offset++;
}

INTERNAL uint32_t
RenderVoxels_Indirect(void* backend, VkCommandBuffer cmd, const Mesh_Pass* mesh_pass, uint32_t num_sets, VkDescriptorSet* sets, size_t num_draws)
{
  Voxel_Backend_Indirect* drawer = backend;
  Graphics_Pipeline* prog;
  if (mesh_pass->flags & MESH_PASS_USE_NORMALS) {
    prog = GetComponent(Graphics_Pipeline, g_voxel_pipeline_indirect);
  } else {
    prog = GetComponent(Graphics_Pipeline, g_voxel_pipeline_shadow);
  }
  // bind buffers
  VkDeviceSize offsets[] = {
    0,
    drawer->start_transform_offset * sizeof(Transform),
    Log2_u32(mesh_pass->cull_mask) * num_draws * sizeof(VX_Vertex_Count)
  };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer, drawer->vertex_count_buffer };
  vkCmdBindVertexBuffers(cmd, 0, (mesh_pass->flags & MESH_PASS_USE_NORMALS) ? 3 : 2, buffers, offsets);
  vkCmdBindIndexBuffer(cmd, drawer->index_buffer, 0, VK_INDEX_TYPE_UINT32);

  // bind pipeline
  cmdBindGraphics(cmd, prog, num_sets, sets);

  if (!drawer->enabled_KHR_draw_indirect_count) {
    // submit draw commands
    uint32_t draw_calls = num_draws * 3;
    // HACK: I hate std140
    const uint32_t stride = 32;
    uint32_t offset = Log2_u32(mesh_pass->cull_mask) * draw_calls * stride;
    VkBuffer buffer = drawer->indirect_buffer;
    vkCmdDrawIndexedIndirect(cmd, buffer,
                             offset,
                             draw_calls,
                             stride);
  } else {
    uint32_t draw_calls = num_draws * 5;
    // HACK: I hate std140
    const uint32_t stride = 32;
    uint32_t offset = Log2_u32(mesh_pass->cull_mask) * draw_calls * stride;
    uint32_t count_offset = MAX_MESH_PASSES * draw_calls * stride +
      Log2_u32(mesh_pass->cull_mask) * 16; // 16 stands for stride. I don't now why uints should be padded 16 but ok, fine, I'm not angry
    vkCmdDrawIndexedIndirectCountKHR(cmd,
                                     drawer->indirect_buffer, offset,
                                     drawer->indirect_buffer, count_offset,
                                     draw_calls, stride);
  }
  return 1;
}

INTERNAL void
CullPass_Indirect(void* backend, VkCommandBuffer cmd, const Mesh_Pass* mesh_passes, uint32_t num_passes,
                  size_t num_draws)
{
  Voxel_Backend_Indirect* drawer = backend;
  Compute_Pipeline* prog;
  if (drawer->enabled_KHR_draw_indirect_count) {
    // fill draw counts with 0
    uint32_t dst_offset = MAX_MESH_PASSES * num_draws * 5 * 32;
    const uint32_t uint_stride = 16;
    vkCmdFillBuffer(cmd, drawer->indirect_buffer, dst_offset, MAX_MESH_PASSES * uint_stride, 0);
    cmdExecutionBarrier(cmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    struct {
      Vec3 camera_front;
      uint32_t cull_mask;
      Vec3 camera_position;
      uint32_t pass_id;
      uint32_t out_offset;
      uint32_t in_offset;
      uint32_t num_draws;
    } push_constant;

    EID last = ENTITY_NIL;
    for (uint32_t i = 0; i < num_passes; i++) {
      EID pipeline_id = (mesh_passes[i].flags & MESH_PASS_PERSP) ? g_voxel_pipeline_compute_ext_persp : g_voxel_pipeline_compute_ext_ortho;
      if (pipeline_id != last) {
        prog = GetComponent(Compute_Pipeline, pipeline_id);
        cmdBindCompute(cmd, prog, 1, &drawer->ds_set);
        last = pipeline_id;
      }
      push_constant.cull_mask = mesh_passes[i].cull_mask;
      push_constant.camera_front = mesh_passes[i].camera_dir;
      push_constant.camera_position = mesh_passes[i].camera_pos;
      push_constant.pass_id = dst_offset / uint_stride + Log2_u32(mesh_passes[i].cull_mask);
      push_constant.out_offset = Log2_u32(mesh_passes[i].cull_mask) * num_draws;
      push_constant.in_offset = drawer->draw_offset - num_draws;
      push_constant.num_draws = num_draws;
      vkCmdPushConstants(cmd, prog->layout, VK_SHADER_STAGE_COMPUTE_BIT,
                         0, sizeof(push_constant), &push_constant);
      vkCmdDispatch(cmd, (num_draws+63) / 64, 1, 1);
    }
  } else {
    prog = GetComponent(Compute_Pipeline, g_voxel_pipeline_compute);
    struct {
      Vec3 camera_front;
      uint32_t cull_mask;
      Vec3 camera_position;
      uint32_t out_offset;
      uint32_t in_offset;
      uint32_t num_draws;
    } push_constant;
    cmdBindCompute(cmd, prog, 1, &drawer->ds_set);
    for (uint32_t i = 0; i < num_passes; i++) {
      push_constant.cull_mask = mesh_passes[i].cull_mask;
      push_constant.camera_front = mesh_passes[i].camera_dir;
      push_constant.camera_position = mesh_passes[i].camera_pos;
      push_constant.out_offset = Log2_u32(mesh_passes[i].cull_mask) * num_draws;
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


/// Voxel drawer

INTERNAL VkResult
SetVoxelBackend_Slow(Voxel_Drawer* drawer, Deletion_Queue* dq)
{
  // HACK: this works for now, but we'd want to do something better
  FOREACH_COMPONENT(Voxel_Cached) {
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

  drawer->new_frame_func = NewFrameVoxel_Slow;
  drawer->clear_cache_func = ClearCacheVoxel_Slow;
  drawer->regenerate_mesh_func = RegenerateVoxel_Slow;
  drawer->push_mesh_func = PushMeshVoxel_Slow;
  drawer->render_voxels_func = RenderVoxels_Slow;
  drawer->cull_pass_func = CullPass_Slow;
  drawer->destroy_func = DestroyVoxel_Slow;

  LOG_INFO("classic voxel drawing backend set");
  return err;
}

INTERNAL VkResult
SetVoxelBackend_Indirect(Voxel_Drawer* drawer, Deletion_Queue* dq)
{
  // HACK: this works for now, but we'd want to do something better
  FOREACH_COMPONENT(Voxel_Cached) {
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

  g_voxel_pipeline_classic = CreateEntity(g_ecs);
  g_voxel_pipeline_indirect = CreateEntity(g_ecs);
  g_voxel_pipeline_shadow = CreateEntity(g_ecs);
  g_voxel_pipeline_compute = CreateEntity(g_ecs);
  g_voxel_pipeline_compute_ext_ortho = CreateEntity(g_ecs);
  g_voxel_pipeline_compute_ext_persp = CreateEntity(g_ecs);

  return err;
}

INTERNAL void
DestroyVoxelDrawer(Voxel_Drawer* drawer)
{
  drawer->destroy_func(&drawer->backend, NULL);
  FreeVideoMemory(&drawer->gpu_memory);
  FreeVideoMemory(&drawer->cpu_memory);
}

INTERNAL void
NewVoxelDrawerFrame(Voxel_Drawer* drawer)
{
  drawer->new_frame_func(&drawer->backend);
  drawer->num_draws = 0;
}

INTERNAL void
ClearVoxelDrawerCache(Voxel_Drawer* drawer)
{
  drawer->clear_cache_func(&drawer->backend);
}

INTERNAL void
PushMeshToVoxelDrawer(Voxel_Drawer* drawer, ECS* ecs, EID entity)
{
  drawer->push_mesh_func(&drawer->backend, ecs, entity);
  drawer->num_draws++;
}

INTERNAL uint32_t
DrawVoxels(Voxel_Drawer* drawer, VkCommandBuffer cmd, const Mesh_Pass* mesh_pass,
           uint32_t num_sets, VkDescriptorSet* sets)
{
  return drawer->render_voxels_func(&drawer->backend, cmd, mesh_pass,
                                    num_sets, sets, drawer->num_draws);
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
CullPass(VkCommandBuffer cmd, Voxel_Drawer* drawer, const Mesh_Pass* mesh_passes, uint32_t num_passes)
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
    .msaa_samples = g_forward_pass->msaa_samples,
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
    .cullMode = VK_CULL_MODE_NONE,
    // .cullMode = VK_CULL_MODE_FRONT_BIT,
    .depth_bias_enable = VK_FALSE,
    .msaa_samples = g_forward_pass->msaa_samples,
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
    .msaa_samples = VK_SAMPLE_COUNT_1_BIT,
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
  memset(grid->data->ptr, fill, grid->width*grid->height*grid->depth);
}
