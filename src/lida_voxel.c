/*
  lida_voxel.c

  Voxel loading, creation, rendering etc.

 */

typedef uint8_t Voxel;
#define VX_USE_INDICES 1
#define VX_USE_CULLING 1
#define VX_USE_INDIRECT 1

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

#if VX_USE_INDIRECT

typedef struct {

  uint32_t first_vertex;
  uint32_t instance_count;
  uint32_t first_instance;
  uint32_t vertex_count[6];
  uint32_t cull_mask;
  // HACK TODO: mess with std140
  char padding[8];

} VX_Draw_Data;

typedef struct {

  uint32_t count0;
  uint32_t count1;
  uint32_t count2;
  uint32_t count3;
  uint32_t count4;
  char padding[12];

} VX_Vertex_Count;

#else

typedef struct {

  // this is for vkCmdDraw
  uint32_t vertexCount;
  uint32_t firstVertex;
  uint32_t firstInstance;
  uint32_t instanceCount;

} VX_Draw_Command;

#endif

typedef struct {

  Video_Memory cpu_memory;
  Video_Memory gpu_memory;
  VkBuffer vertex_buffer;
  VkBuffer transform_buffer;
  VkBuffer index_buffer;
#if VX_USE_INDIRECT
  VkBuffer storage_buffer;
  VkBuffer indirect_buffer;
  VkBuffer vertex_count_buffer;
  VkDescriptorSet ds_set;
  VX_Draw_Data* pDraws;
#endif

  Vertex_X3C* pVertices;
  Transform* pTransforms;
  uint32_t* pIndices;
  size_t max_vertices;
  size_t max_draws;
  size_t max_indices;

  // reset each frame
  size_t vertex_offset;
  size_t transform_offset;
  size_t start_transform_offset;
#if VX_USE_INDIRECT
  size_t draw_offset;
#endif

#if !VX_USE_INDIRECT
  Allocation* draws;
#endif
  size_t num_draws;
  Allocation* meshes;
  size_t num_meshes;

} Voxel_Drawer;

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

typedef struct {

  uint64_t hash;
  uint32_t first_vertex;
  uint32_t offsets[6];
  int cull_mask;

} Voxel_Cached;
DECLARE_COMPONENT(Voxel_Cached);

// NOTE: this doesn't do bounds checking
// NOTE: setting a voxel value with this macro is unsafe, hash won't be correct,
// consider using SetInVoxelGrid
#define GetInVoxelGrid(grid, x, y, z) ((Voxel*)(grid)->data->ptr)[(x) + (y)*(grid)->width + (z)*(grid)->width*(grid)->height]

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
        // grow quad while all voxels in that quad are the same
        while (pos[v] < dims[v]) {
          pos[u] = i;
          if (GetInVoxelGrid(grid, pos[0], pos[1], pos[2]) != start_voxel)
            break;
          pos[u]++;
          while (pos[u] < min_i &&
                 GetInVoxelGrid(grid, pos[0], pos[1], pos[2]) == start_voxel) {
            pos[u]++;
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


/// Voxel drawer

INTERNAL VkResult
CreateVoxelDrawer(Voxel_Drawer* drawer, Allocator* allocator, uint32_t max_vertices, uint32_t max_draws)
{
  PROFILE_FUNCTION();
#if VX_USE_INDIRECT
  drawer->meshes = DoAllocation(allocator, max_draws * sizeof(EID),
                                "voxel-mesh-ids");
  drawer->num_meshes = 0;
#else
  drawer->draws = DoAllocation(allocator, 6 * max_draws * sizeof(VX_Draw_Command),
                               "voxel-draws");
  drawer->meshes = DoAllocation(allocator, max_draws * sizeof(EID),
                                "voxel-mesh-ids");
  drawer->num_draws = 0;
  drawer->num_meshes = 0;
  if (drawer->draws == 0 || drawer->meshes == 0) {
    LOG_ERROR("out of memory");
    return VK_ERROR_OUT_OF_HOST_MEMORY;
  }
#endif
  // create buffers
  VkResult err = CreateBuffer(&drawer->vertex_buffer, max_vertices * sizeof(Vertex_X3C),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/vertex-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create vertex buffer for storing voxel meshes with error %s",
              ToString_VkResult(err));
    return err;
  }
  err = CreateBuffer(&drawer->transform_buffer, max_draws * sizeof(Transform),
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "voxel-drawer/transform-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("faled to create vertex buffer for storing voxel transforms with error %s",
              ToString_VkResult(err));
    return err;
  }
  drawer->max_indices = max_vertices*3/2;
  err = CreateBuffer(&drawer->index_buffer, drawer->max_indices * sizeof(uint32_t),
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "voxel-draw/index-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("faled to create index buffer for drawing voxels with error %s",
              ToString_VkResult(err));
    return err;
  }
#if VX_USE_INDIRECT
  err = CreateBuffer(&drawer->storage_buffer, max_draws * sizeof(VX_Draw_Data),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "voxel-draw/storage-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("faled to create storage buffer for drawing voxels with error %s",
              ToString_VkResult(err));
    return err;
  }
  // NOTE: 32 is max number of passes
  err = CreateBuffer(&drawer->indirect_buffer, 32 * max_draws * /*sizeof(VkDrawIndexedIndirectCommand)*/32,
                     VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "voxel-draw/indirect-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("faled to create indirect buffer for drawing voxels with error %s",
              ToString_VkResult(err));
    return err;
  }
  err = CreateBuffer(&drawer->vertex_count_buffer, 32 * max_draws * sizeof(VX_Vertex_Count),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-draw/vertex-count-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("faled to create vertex count buffer with error %s",
              ToString_VkResult(err));
    return err;
  }
#endif
  // allocate memory
#if VX_USE_INDICES
# if VX_USE_INDIRECT
  VkMemoryRequirements buffer_requirements[4];
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_buffer, &buffer_requirements[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->transform_buffer, &buffer_requirements[1]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->index_buffer, &buffer_requirements[2]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->storage_buffer, &buffer_requirements[3]);
# else
  VkMemoryRequirements buffer_requirements[3];
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_buffer, &buffer_requirements[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->transform_buffer, &buffer_requirements[1]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->index_buffer, &buffer_requirements[2]);
# endif
#else
# if VX_USE_INDIRECT
# error Draw indirect is not supported for drawing without indices
# endif
  VkMemoryRequirements buffer_requirements[2];
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_buffer, &buffer_requirements[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->transform_buffer, &buffer_requirements[1]);
#endif
  VkMemoryRequirements requirements;
  MergeMemoryRequirements(buffer_requirements, ARR_SIZE(buffer_requirements), &requirements);
  // try to allocate device local memory accessible from CPU
  VkMemoryPropertyFlags required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  err = AllocateVideoMemory(&drawer->cpu_memory, requirements.size,
                            required_flags|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            requirements.memoryTypeBits,
                            "voxel-drawer/memory");
  if (err != VK_SUCCESS) {
    // if failed try to allocate any memory accessible from CPU
    err = AllocateVideoMemory(&drawer->cpu_memory, requirements.size,
                              required_flags,
                              requirements.memoryTypeBits,
                              "voxel-drawer/memory");
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to allocate video memory for voxels with error %s", ToString_VkResult(err));
      return err;
    }
  }
  LOG_TRACE("allocated %u bytes for voxels", (uint32_t)requirements.size);
#if VX_USE_INDIRECT
  VkMemoryRequirements buffer_requirements_gpu[2];
  // allocate device local memory for buffers that we will not be accessing from CPU
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->indirect_buffer, &buffer_requirements_gpu[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_count_buffer, &buffer_requirements_gpu[1]);
  MergeMemoryRequirements(buffer_requirements_gpu, 2, &requirements);
  err = AllocateVideoMemory(&drawer->gpu_memory, requirements.size,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            requirements.memoryTypeBits,
                            "voxel-drawer/fast-memory");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate video memory for voxels with error %s", ToString_VkResult(err));
    return err;
  }
#endif
  // bind buffers to allocated memory
  err = BufferBindToMemory(&drawer->cpu_memory, drawer->vertex_buffer,
                           &buffer_requirements[0], (void**)&drawer->pVertices, NULL);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to bind vertex buffer to memory with error %s", ToString_VkResult(err));
  }
  err = BufferBindToMemory(&drawer->cpu_memory, drawer->transform_buffer,
                           &buffer_requirements[1], (void**)&drawer->pTransforms, NULL);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to bind storage buffer to memory with error %s", ToString_VkResult(err));
  }
#if VX_USE_INDICES
  err = BufferBindToMemory(&drawer->cpu_memory, drawer->index_buffer,
                           &buffer_requirements[2], (void**)&drawer->pIndices, NULL);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to bind index buffer to memory with error %s", ToString_VkResult(err));
  }
#endif
#if VX_USE_INDIRECT
  err = BufferBindToMemory(&drawer->cpu_memory, drawer->storage_buffer,
                           &buffer_requirements[3], (void**)&drawer->pDraws, NULL);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to bind storage buffer to memory with error %s", ToString_VkResult(err));
  }
  err = BufferBindToMemory(&drawer->gpu_memory, drawer->indirect_buffer,
                           &buffer_requirements_gpu[0], NULL, NULL);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to bind indirect buffer to memory with error %s", ToString_VkResult(err));
  }
  err = BufferBindToMemory(&drawer->gpu_memory, drawer->vertex_count_buffer,
                           &buffer_requirements_gpu[1], NULL, NULL);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to bind vertex count buffer to memory with error %s", ToString_VkResult(err));
  }
#endif

#if VX_USE_INDIRECT
  // create descriptor set
  VkDescriptorSetLayoutBinding bindings[4];
  for (uint32_t i = 0; i < ARR_SIZE(bindings); i++)
    bindings[i] = (VkDescriptorSetLayoutBinding) {
      .binding = i,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
  err = AllocateDescriptorSets(bindings, ARR_SIZE(bindings), &drawer->ds_set, 1, 0, "voxel/cull-set");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to allocate descriptor set with error %s", ToString_VkResult(err));
    return err;
  }
  // update descriptor set
  VkWriteDescriptorSet write_sets[4];
  VkDescriptorBufferInfo buffer_infos[4];
  VkBuffer buffers[] = { drawer->storage_buffer, drawer->transform_buffer, drawer->indirect_buffer, drawer->vertex_count_buffer };
  for (size_t i = 0; i < ARR_SIZE(write_sets); i++) {
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
  UpdateDescriptorSets(write_sets, ARR_SIZE(write_sets));
#endif

  // init fields
  drawer->max_draws = max_draws;
  drawer->max_vertices = max_vertices;
  drawer->vertex_offset = 0;
  return err;
}

INTERNAL void
DestroyVoxelDrawer(Voxel_Drawer* drawer, Allocator* allocator)
{
  PROFILE_FUNCTION();
  FreeAllocation(allocator, drawer->meshes);
#if !VX_USE_INDIRECT
  FreeAllocation(allocator, drawer->draws);
#endif
#if VX_USE_INDIRECT
  vkDestroyBuffer(g_device->logical_device, drawer->vertex_count_buffer, NULL);
  vkDestroyBuffer(g_device->logical_device, drawer->indirect_buffer, NULL);
  vkDestroyBuffer(g_device->logical_device, drawer->storage_buffer, NULL);
#endif
  vkDestroyBuffer(g_device->logical_device, drawer->index_buffer, NULL);
  vkDestroyBuffer(g_device->logical_device, drawer->transform_buffer, NULL);
  vkDestroyBuffer(g_device->logical_device, drawer->vertex_buffer, NULL);
  FreeVideoMemory(&drawer->cpu_memory);
  FreeVideoMemory(&drawer->gpu_memory);
}

INTERNAL void
NewVoxelDrawerFrame(Voxel_Drawer* drawer)
{
  PROFILE_FUNCTION();
  if ((g_window->frame_counter & 1) == 0) {
    drawer->transform_offset = 0;
#if VX_USE_INDIRECT
    drawer->draw_offset = 0;
#endif
  }
  drawer->num_draws = 0;
  drawer->num_meshes = 0;
  drawer->start_transform_offset = drawer->transform_offset;
}

INTERNAL void
ClearVoxelDrawerCache(Voxel_Drawer* drawer)
{
  drawer->vertex_offset = 0;
}

INTERNAL void
VX_Regenerate(Voxel_Drawer* drawer, Voxel_Cached* cached, Voxel_Grid* grid)
{
  cached->hash = grid->hash;

#if VX_USE_INDIRECT

  uint32_t base_index = 0;
  VX_Draw_Data* draw = &drawer->pDraws[drawer->draw_offset++];
  draw->first_vertex = drawer->vertex_offset;
  draw->instance_count = 1;
  // draw->first_instance = drawer->transform_offset;
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

#else
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
#endif
}

INTERNAL void
PushMeshToVoxelDrawer(Voxel_Drawer* drawer, ECS* ecs, EID entity)
{
  PROFILE_FUNCTION();
  uint32_t index = drawer->num_meshes;
  // add transform
  Transform* transform = GetComponent(Transform, entity);
  memcpy(&drawer->pTransforms[drawer->transform_offset],
         transform, sizeof(Transform));
  EID* meshes = drawer->meshes->ptr;
  Voxel_Grid* grid = GetComponent(Voxel_Grid, entity);
#if !VX_USE_INDIRECT
  VX_Draw_Command* draws = drawer->draws->ptr;
#endif

  // try to use instancing if same mesh is pushed twice
  if (index > 0) {
    Voxel_Cached* prev = GetComponent(Voxel_Cached, meshes[index-1]);
    if (prev->hash == grid->hash) {
#if VX_USE_INDIRECT
      VX_Draw_Data* draw = &drawer->pDraws[drawer->draw_offset];
      draw->instance_count++;
      Voxel_Cached* cached = GetComponent(Voxel_Cached, entity);
      if (cached) {
        draw->cull_mask |= cached->cull_mask;
      }
#else
      for (uint32_t i = 0; i < 6; i++) {
        draws[drawer->num_draws-6+i].instanceCount++;
      }
#endif
      goto end;
    }
  }

  // try to use cache
  Voxel_Cached* cached = GetComponent(Voxel_Cached, entity);
  if (cached) {
    if (cached->hash != grid->hash || cached->first_vertex > drawer->vertex_offset) {
      // regenerate if grid changed
      VX_Regenerate(drawer, cached, grid);
      goto end;
    }
#if VX_USE_INDIRECT
    VX_Draw_Data* draw = &drawer->pDraws[drawer->draw_offset++];
    draw->first_vertex = cached->first_vertex;
    draw->instance_count = 1;
    draw->first_instance = drawer->transform_offset - drawer->start_transform_offset;
    draw->cull_mask = cached->cull_mask;
    for (uint32_t i = 0; i < 6; i++) {
      draw->vertex_count[i] = cached->offsets[i];
    }
#else
    uint32_t vertex_offset = cached->first_vertex;
    for (uint32_t i = 0; i < 6; i++) {
      VX_Draw_Command* command = &draws[drawer->num_draws++];
      command->firstVertex = vertex_offset;
      command->vertexCount = cached->offsets[i];
      command->firstInstance = drawer->transform_offset - drawer->start_transform_offset;
      command->instanceCount = 1;
      vertex_offset += cached->offsets[i];
    }
#endif
  } else {
    // didn't succeed, generate new vertices
    cached = AddComponent(ecs, Voxel_Cached, entity);
    VX_Regenerate(drawer, cached, grid);
  }
 end:
#if VX_USE_INDIRECT
  drawer->num_draws++;
#endif
  drawer->transform_offset++;
  meshes[drawer->num_meshes++] = entity;
}

// return: number of drawcalls
INTERNAL uint32_t
DrawVoxels(Voxel_Drawer* drawer, VkCommandBuffer cmd, const Mesh_Pass* mesh_pass, EID pipeline)
{
  PROFILE_FUNCTION();
  Graphics_Pipeline* prog = GetComponent(Graphics_Pipeline, pipeline);
  // bind vertices
#if VX_USE_INDIRECT
  (void)prog;
  VkDeviceSize offsets[] = {
    0,
    drawer->start_transform_offset * sizeof(Transform),
    Log2_u32(mesh_pass->cull_mask) * drawer->num_draws * sizeof(VX_Vertex_Count)
  };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer, drawer->vertex_count_buffer };
  vkCmdBindVertexBuffers(cmd, 0, (mesh_pass->flags & MESH_PASS_USE_NORMALS) ? 3 : 2, buffers, offsets);
#else
  VkDeviceSize offsets[] = { 0, drawer->start_transform_offset * sizeof(Transform) };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer };
  vkCmdBindVertexBuffers(cmd, 0, ARR_SIZE(buffers), buffers, offsets);
#endif

#if VX_USE_INDICES
  vkCmdBindIndexBuffer(cmd, drawer->index_buffer, 0, VK_INDEX_TYPE_UINT32);
#endif
#if VX_USE_INDIRECT

  uint32_t draw_calls = drawer->num_draws * 3;
  // HACK: I hate std140
  const uint32_t stride = 32;
  uint32_t offset = Log2_u32(mesh_pass->cull_mask) * draw_calls * stride;
  VkBuffer buffer = drawer->indirect_buffer;
  vkCmdDrawIndexedIndirect(cmd, buffer,
                           offset,
                           draw_calls,
                           stride);

#else
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
#endif
  return draw_calls;
}

INTERNAL void
PipelineVoxelVertices(const VkVertexInputAttributeDescription** attributes, uint32_t* num_attributes,
                      const VkVertexInputBindingDescription** bindings, uint32_t* num_bindings,
                      int using_colors)
{
  GLOBAL VkVertexInputBindingDescription g_bindings[] = {
    { 0, sizeof(Vertex_X3C), VK_VERTEX_INPUT_RATE_VERTEX },
    { 1, sizeof(Transform), VK_VERTEX_INPUT_RATE_INSTANCE },
#if VX_USE_INDIRECT
    { 2, sizeof(VX_Vertex_Count), VK_VERTEX_INPUT_RATE_INSTANCE }
#endif
  };
  GLOBAL VkVertexInputAttributeDescription g_attributes1[] = {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex_X3C, position) },
    { 1, 0, VK_FORMAT_R32_UINT, offsetof(Vertex_X3C, color) },
    { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Transform, rotation) },
    { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Transform, position) },
    { 4, 1, VK_FORMAT_R32_SFLOAT, offsetof(Transform, scale) },
#if VX_USE_INDIRECT
    { 5, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count0) },
    { 6, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count1) },
    { 7, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count2) },
    { 8, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count3) },
    { 9, 2, VK_FORMAT_R32_UINT, offsetof(VX_Vertex_Count, count4) },
#endif
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
#if VX_USE_INDIRECT
  *num_bindings = (using_colors ? 3 : 2);
#else
  *num_bindings = ARR_SIZE(g_bindings);
#endif
}

// calculate oriented bounding box's corners
INTERNAL void
CalculateVoxelGridOBB(const Voxel_Grid* grid, const Transform* transform, OBB* obb)
{
  Vec3 half_size;
  CalculateVoxelGridSize(grid, &half_size);
  CalculateObjectOBB(&half_size, transform, obb);
}

#if VX_USE_INDIRECT
INTERNAL void
MeshPass(VkCommandBuffer cmd, Voxel_Drawer* drawer, const Mesh_Pass* mesh_pass, VkPipelineLayout pipeline_layout)
{
  struct {
    Vec3 camera_front;
    uint32_t cull_mask;
    Vec3 camera_position;
    uint32_t out_offset;
    uint32_t in_offset;
    uint32_t num_draws;
  } push_constant;
  push_constant.cull_mask = mesh_pass->cull_mask;
  push_constant.camera_front = mesh_pass->camera_dir;
  push_constant.camera_position = mesh_pass->camera_pos;
  push_constant.out_offset = Log2_u32(mesh_pass->cull_mask) * drawer->num_draws;
  push_constant.in_offset = drawer->draw_offset - drawer->num_draws;
  push_constant.num_draws = drawer->num_draws;
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout,
                          0, 1, &drawer->ds_set, 0, NULL);
  vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                     0, sizeof(push_constant), &push_constant);
  vkCmdDispatch(cmd, (drawer->num_draws+63) / 64, 1, 1);
}
#endif
