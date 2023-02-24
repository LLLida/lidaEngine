/*
  lida_voxel.c

  Voxel loading, creation, rendering etc.

 */

typedef uint8_t Voxel;

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

// 16 bytes
typedef struct {

  Vec3 position;
  uint32_t color;

} Vertex_X3C;

typedef struct {

  // this is for vkCmdDraw
  uint32_t vertexCount;
  uint32_t firstVertex;
  uint32_t firstInstance;
  uint32_t instanceCount;
  // this is for culling
  // vec3 normalVector;
  // vec3 position;

} VX_Draw_Command;

typedef struct {

  uint64_t hash;
  uint32_t first_vertex;
  uint32_t last_vertex;
  uint32_t first_draw_id;

} VX_Mesh_Info;


typedef struct {
  uint32_t draw_id;
  uint32_t first_vertex;
  uint32_t last_vertex;
} VX_Draw_ID;

typedef struct {
  uint32_t draw_id;
  // counter for robin-hood hashing
  uint32_t psl;
} VX_Draw_Hash;

typedef struct {

  Video_Memory memory;
  VkBuffer vertex_buffer;
  VkBuffer transform_buffer;
  // TODO: use index buffer
  // VkBuffer index_buffer;

  Vertex_X3C* pVertices;
  Transform* pTransforms;
  size_t max_vertices;
  size_t max_draws;

  int frame_id;
  size_t vertex_offset;
  size_t transform_offset;

  Vertex_X3C* vertex_temp_buffer;
  size_t vertex_temp_buffer_size;

  struct {
    VX_Draw_Command* draws;
    size_t num_draws;
    VX_Mesh_Info* meshes;
    size_t num_meshes;
  } frames[2];

  VX_Draw_Hash* hashes_cached;
  size_t num_hashes_cached;
  VX_Draw_ID* regions_cached;
  size_t num_regions_cached;

} Voxel_Drawer;

// NOTE: this doesn't do bounds checking
// NOTE: setting a voxel value with this macro is unsafe, hash won't be correct,
// consider using SetInVoxelGrid
#define GetInVoxelGrid(grid, x, y, z) ((Voxel*)(grid)->data->ptr)[(x) + (y)*(grid)->width + (z)*(grid)->width*(grid)->height]


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

GLOBAL const iVec3 vox_normals[6] = {
  {-1, 0, 0},
  {1, 0, 0},
  {0, -1, 0},
  {0, 1, 0},
  {0, 0, -1},
  {0, 0, 1}
};

INTERNAL uint32_t
GetVoxelGridMaxGeneratedVertices(const Voxel_Grid* grid)
{
  // I made some conclusions and I certain that at most half voxels are written in worst case.
  // However, we need a mathematical proof.
  return 3 * grid->width * grid->height * grid->depth;
}

INTERNAL uint32_t
GenerateVoxelGridMeshNaive(const Voxel_Grid* grid, Vertex_X3C* vertices, int face)
{
  Vertex_X3C* begin = vertices;
  Vec3 half_size = {
    grid->width *  0.5f,
    grid->height * 0.5f,
    grid->depth * 0.5f
  };
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
          Vec3 pos = VEC3_SUB(VEC3_CREATE((float)x, (float)y, (float)z), half_size);
          // TODO: use index buffers
          for (uint32_t i = 0; i < 6; i++) {
            vertices[i].position = VEC3_ADD(pos, vox_positions[face*6 + i]);
            vertices[i].color = grid->palette[voxel];
          }
          vertices += 6;
        }
      }
  LOG_DEBUG("wrote %u vertices", (uint32_t)(vertices - begin));
  return vertices - begin;
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
GenerateVoxelGridMeshGreedy(const Voxel_Grid* grid, Vertex_X3C* vertices, int face)
{
  // TODO: my dream is to make this function execute fast, processing
  // 4 or 8 voxels at same time
  Vertex_X3C* begin = vertices;
  float inv_size;
  // TODO: we currently have no Min() function defined... shame
  if (grid->width <= grid->height && grid->height <= grid->depth) {
    inv_size = 1.0f / (float)grid->width;
  } else if (grid->height <= grid->width && grid->width <= grid->depth) {
    inv_size = 1.0f / (float)grid->height;
  } else {
    inv_size = 1.0f / (float)grid->depth;
  }
  Vec3 half_size = {
    inv_size * 0.5f * (float)grid->width,
    inv_size * 0.5f * (float)grid->height,
    inv_size * 0.5f * (float)grid->depth,
  };
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
        // mark merged voxels
        for (uint32_t jj = j; jj < pos[v]; jj++)
          for (uint32_t ii = i; ii < min_i; ii++) {
            merged_mask[ii + jj * dims[u]] = 1;
          }
      }
  }
  PersistentRelease(merged_mask);
  LOG_DEBUG("wrote %u vertices", (uint32_t)(vertices - begin));
  return vertices - begin;
}

INTERNAL int
LoadVoxelGrid(Allocator* allocator, Voxel_Grid* grid, const uint8_t* buffer, uint32_t size)
{
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
  size_t buff_size;
  uint8_t* buffer = (uint8_t*)PlatformLoadEntireFile(filename, &buff_size);
  if (buffer == NULL) {
    LOG_WARN("failed to open file '%s' for voxel model loading", filename);
    return -1;
  }
  int ret = LoadVoxelGrid(allocator, grid, buffer, buff_size);
  PlatformFreeFile(buffer);
  return ret;
}


/// Voxel drawer

INTERNAL VkResult
CreateVoxelDrawer(Voxel_Drawer* drawer, uint32_t max_vertices, uint32_t max_draws)
{

  for (int i = 0; i < 2; i++) {
    // TODO: use allocator
    drawer->frames[i].draws = PersistentAllocate(6 * max_draws * sizeof(VX_Draw_Command));
    drawer->frames[i].meshes = PersistentAllocate(max_vertices * sizeof(VX_Mesh_Info));
    drawer->frames[i].num_draws = 0;
    drawer->frames[i].num_meshes = 0;
  }
  // TODO: I think, we're allocating too much memory for caches. Hoping
  // we will have a good allocator soon.
  drawer->hashes_cached = PersistentAllocate(NearestPow2(max_draws) * sizeof(VX_Draw_Hash));
  drawer->regions_cached = PersistentAllocate(max_draws * sizeof(VX_Draw_ID));
  drawer->num_hashes_cached = 0;
  drawer->num_regions_cached = 0;
  // create buffers
  VkResult err = CreateBuffer(&drawer->vertex_buffer, max_vertices * sizeof(Vertex_X3C),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/vertex-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to create vertex buffer for storing voxel meshes with error %s",
              ToString_VkResult(err));
    return err;
  }
  err = CreateBuffer(&drawer->transform_buffer, max_draws * sizeof(Transform),
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/transform-buffer");
  if (err != VK_SUCCESS) {
    LOG_ERROR("faled to create storage buffer for storing voxel transforms with error %s",
              ToString_VkResult(err));
    return err;
  }
  // allocate memory
  VkMemoryRequirements buffer_requirements[2];
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->vertex_buffer, &buffer_requirements[0]);
  vkGetBufferMemoryRequirements(g_device->logical_device,
                                drawer->transform_buffer, &buffer_requirements[1]);
  VkMemoryRequirements requirements;
  MergeMemoryRequirements(buffer_requirements, ARR_SIZE(buffer_requirements), &requirements);
  // try to allocate device local memory accessible from CPU
  VkMemoryPropertyFlags required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  err = AllocateVideoMemory(&drawer->memory, requirements.size,
                            required_flags|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            requirements.memoryTypeBits,
                            "voxel-drawer/memory");
  if (err != VK_SUCCESS) {
    // if failed try to allocate any memory accessible from CPU
    err = AllocateVideoMemory(&drawer->memory, requirements.size,
                              required_flags,
                              requirements.memoryTypeBits,
                              "voxel-drawer/memory");
    if (err != VK_SUCCESS) {
      LOG_ERROR("failed to allocate video memory for voxels with error %s", ToString_VkResult(err));
      return err;
    }
  }
  // bind buffers to allocated memory
  err = BufferBindToMemory(&drawer->memory, drawer->vertex_buffer,
                           &buffer_requirements[0], (void**)&drawer->pVertices, NULL);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to bind vertex buffer to memory with error %s", ToString_VkResult(err));
  }
  err = BufferBindToMemory(&drawer->memory, drawer->transform_buffer,
                                &buffer_requirements[1], (void**)&drawer->pTransforms, NULL);
  if (err != VK_SUCCESS) {
    LOG_WARN("failed to bind storage buffer to memory with error %s", ToString_VkResult(err));
  }
  // allocate vertex temp buffer
  drawer->vertex_temp_buffer_size = 20 * 1024;
  drawer->vertex_temp_buffer = (Vertex_X3C*)PersistentAllocate(drawer->vertex_temp_buffer_size * sizeof(Vertex_X3C));
  //
  drawer->max_draws = max_draws;
  drawer->max_vertices = max_vertices;
  drawer->vertex_offset = 0;
  drawer->frame_id = 1;
  return err;
}

INTERNAL void
DestroyVoxelDrawer(Voxel_Drawer* drawer)
{
  PersistentRelease(drawer->vertex_temp_buffer);
  vkDestroyBuffer(g_device->logical_device, drawer->transform_buffer, NULL);
  vkDestroyBuffer(g_device->logical_device, drawer->vertex_buffer, NULL);
  FreeVideoMemory(&drawer->memory);
}

INTERNAL int
CompareVX_DrawID(const void* lhs, const void* rhs)
{
  const VX_Draw_ID* l = lhs, *r = rhs;
  return COMPARE(l->first_vertex, r->first_vertex);
}

INTERNAL void
UpdateVoxelDrawerCache(Voxel_Drawer* drawer)
{
  VX_Mesh_Info* prev_meshes = drawer->frames[1-drawer->frame_id].meshes;
  size_t n = drawer->frames[1-drawer->frame_id].num_meshes;
  const uint32_t n2 = NearestPow2(n);

  drawer->num_hashes_cached = n2;
  drawer->num_regions_cached = n+1;

  VX_Draw_Hash* hashes = drawer->hashes_cached;
  VX_Draw_ID* regions = drawer->regions_cached;

  for (size_t  i = 0; i < n; i++) {
    regions[i].draw_id = i;
    regions[i].first_vertex = prev_meshes[i].first_vertex;
    regions[i].last_vertex = prev_meshes[i].last_vertex;
  }
  for (uint32_t i = 0; i < n2; i++) {
    hashes[i].draw_id = UINT32_MAX;
  }
  // build hash table of draws from previous frame using robin-hood hashing
  // https://programming.guide/robin-hood-hashing.html
  for (uint32_t i = 0; i < n; i++) {
    VX_Mesh_Info* mesh = &prev_meshes[i];
    // because n2 is a power of 2, we can do '&' instead of expensive '%'
    uint32_t id = mesh->hash & (n2-1);
    VX_Draw_Hash obj;
    obj.draw_id = i;
    obj.psl = 0;
    while (hashes[id].draw_id != UINT32_MAX) {
      if (obj.psl > hashes[id].psl) {
        MemorySwap(&obj, &hashes[id], sizeof(obj));
      }
      obj.psl++;
      id = (id+1) & (n2-1);
    }
    memcpy(&hashes[id], &obj, sizeof(obj));
  }
  QuickSort(regions, n, sizeof(VX_Draw_ID), &CompareVX_DrawID);
  // upper bound of buffer
  regions[n].draw_id = n;
  VX_Mesh_Info* border = &prev_meshes[n];
  border->first_vertex = drawer->max_vertices;
  border->last_vertex = 0;
  drawer->frames[1-drawer->frame_id].num_meshes++;
}

INTERNAL void
NewVoxelDrawerFrame(Voxel_Drawer* drawer)
{
  drawer->frame_id = 1 - drawer->frame_id;
  if (drawer->frame_id == 0)
    drawer->transform_offset = 0;
  drawer->frames[drawer->frame_id].num_draws = 0;
  drawer->frames[drawer->frame_id].num_meshes = 0;
  UpdateVoxelDrawerCache(drawer);
}

INTERNAL uint32_t*
VX_FindDrawByHash(Voxel_Drawer* drawer, uint64_t hash)
{
  uint32_t n = drawer->num_hashes_cached;
  if (n > 0) {
    // n is always power of two
    VX_Draw_Hash* hashes = (VX_Draw_Hash*)drawer->hashes_cached;
    VX_Mesh_Info* prev_meshes = (VX_Mesh_Info*)drawer->frames[1-drawer->frame_id].meshes;
    uint32_t id = hash & (n-1);
    uint32_t psl = 0;
    while (1) {
      if (hashes[id].draw_id == UINT32_MAX) {
        // empty slot encountered => return NULL
        return NULL;
      }
      if (prev_meshes[hashes[id].draw_id].hash == hash) {
        // we found hash => return
        return &hashes[id].draw_id;
      }
      if (psl > hashes[id].psl) {
        return NULL;
      }
      id = (id+1) & (n-1);
      psl++;
    }
  }
  return NULL;
}

INTERNAL VX_Draw_ID*
VX_FindNearestDraw(Voxel_Drawer* drawer, uint32_t offset)
{
  // based on https://en.cppreference.com/w/cpp/algorithm/upper_bound
  uint32_t n = drawer->num_regions_cached;
  VX_Draw_ID* regions = drawer->regions_cached;
  VX_Draw_ID* ptr = regions;
  VX_Mesh_Info* prev_meshes = drawer->frames[1-drawer->frame_id].meshes;
  while (n > 0) {
    uint32_t step = n / 2;
    VX_Draw_ID* it = ptr + step;
    if (offset >= prev_meshes[it->draw_id].first_vertex) {
      ptr = it+1;
      n -= step + 1;
    } else {
      n = step;
    }
  }
  return ptr;
}

INTERNAL void
PushMeshToVoxelDrawer(Voxel_Drawer* drawer, const Voxel_Grid* grid, const Transform* transform)
{
  uint32_t index = drawer->frames[drawer->frame_id].num_meshes;
  // add transform
  memcpy(&drawer->pTransforms[drawer->transform_offset], transform, sizeof(Transform));
  if (index > 0) {
    // try to use instancing if same mesh is pushed twice
    VX_Mesh_Info* prev_mesh = &drawer->frames[drawer->frame_id].meshes[index-1];
    if (prev_mesh->hash == grid->hash) {
      VX_Draw_Command* draws = drawer->frames[drawer->frame_id].draws;
      for (uint32_t i = 0; i < 6; i++) {
        draws[prev_mesh->first_draw_id+i].instanceCount++;
      }
      drawer->transform_offset++;
      return;
    }
  }
  const uint32_t* draw_id = VX_FindDrawByHash(drawer, grid->hash);
  VX_Mesh_Info* prev_meshes = drawer->frames[1-drawer->frame_id].meshes;
  VX_Mesh_Info* mesh = &drawer->frames[drawer->frame_id].meshes[drawer->frames[drawer->frame_id].num_meshes++];
  if (draw_id) {
    // if we found that a grid with exact same hash was rendered in previous frame,
    // then use previous frame's vertices in this frame.
    // This helps to not wasting time generating vertices every frame.
    VX_Draw_Command* prev_draws = drawer->frames[1-drawer->frame_id].draws;
    memcpy(mesh, &prev_meshes[*draw_id], sizeof(VX_Mesh_Info));
    for (int i = 0; i < 6; i++) {
      VX_Draw_Command* dst = &drawer->frames[drawer->frame_id].draws[drawer->frames[drawer->frame_id].num_draws++];
      VX_Draw_Command* src = &prev_draws[prev_meshes[*draw_id].first_draw_id + i];
      dst->firstVertex = src->firstVertex;
      dst->vertexCount = src->vertexCount;
      dst->firstInstance = drawer->transform_offset;
      dst->instanceCount = 1;
    }
  } else {
    // if hash not found then generate new vertices and draw data.
    mesh->first_vertex = drawer->vertex_offset;
    // upper_bound is first vertex position which will be untouched in worst case scenario
    uint32_t upper_bound = drawer->vertex_offset + GetVoxelGridMaxGeneratedVertices(grid);
    VX_Draw_ID* d = VX_FindNearestDraw(drawer, drawer->vertex_offset);
    for (int i = 0; i < 6; i++) {
      VX_Draw_Command* command = &drawer->frames[drawer->frame_id].draws[drawer->frames[drawer->frame_id].num_draws++];
      if (upper_bound > d->first_vertex) {
        // if we're not sure if we can write voxels safely at this position
        // then write to a temporary buffer and see if we can fit to current free region
        command->vertexCount = GenerateVoxelGridMeshGreedy(grid, drawer->vertex_temp_buffer, i);
        while (drawer->vertex_offset + command->vertexCount > prev_meshes[d->draw_id].first_vertex) {
          drawer->vertex_offset = prev_meshes[d->draw_id].last_vertex;
          d = VX_FindNearestDraw(drawer, drawer->vertex_offset);
        }
        // copy temporary buffer to newly found free region
        memcpy(drawer->pVertices + drawer->vertex_offset,
               drawer->vertex_temp_buffer,
               command->vertexCount * sizeof(Vertex_X3C));
      } else {
        // if there's enough space we can write vertices directly to buffer
        command->vertexCount =
          GenerateVoxelGridMeshGreedy(grid, drawer->pVertices + drawer->vertex_offset, i);
      }
      command->firstVertex = drawer->vertex_offset;
      command->firstInstance = drawer->transform_offset;
      command->instanceCount = 1;
      drawer->vertex_offset += command->vertexCount;
    }
    mesh->last_vertex = drawer->vertex_offset;
    mesh->hash = grid->hash;
  }
  mesh->first_draw_id = 6 * index;
  drawer->transform_offset++;
}

INTERNAL void
DrawVoxels(Voxel_Drawer* drawer, VkCommandBuffer cmd)
{
  VkDeviceSize offsets[] = { 0, 0 };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer };
  vkCmdBindVertexBuffers(cmd, 0, ARR_SIZE(buffers), buffers, offsets);
  VX_Draw_Command* draws = drawer->frames[drawer->frame_id].draws;
  for (uint32_t i = 0; i < drawer->frames[drawer->frame_id].num_draws; i += 6) {
    VX_Draw_Command* command = &draws[i];
    // merge draw calls
    uint32_t vertex_count = 0;
    for (uint32_t j = 0; j < 6; j++) {
      vertex_count += command[j].vertexCount;
    }
    vkCmdDraw(cmd,
              vertex_count, command->instanceCount,
              command->firstVertex, command->firstInstance);
  }
}

INTERNAL void
DrawVoxelsWithNormals(Voxel_Drawer* drawer, VkCommandBuffer cmd, uint32_t normal_id)
{
  VkDeviceSize offsets[] = { 0, 0 };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer };
  vkCmdBindVertexBuffers(cmd, 0, ARR_SIZE(buffers), buffers, offsets);
  VX_Draw_Command* draws = drawer->frames[drawer->frame_id].draws;
  for (uint32_t i = normal_id; i < drawer->frames[drawer->frame_id].num_draws; i += 6) {
    VX_Draw_Command* command = &draws[i];
    vkCmdDraw(cmd,
              command->vertexCount, command->instanceCount,
              command->firstVertex, command->firstInstance);
  }
}

INTERNAL void
PipelineVoxelVertices(const VkVertexInputAttributeDescription** attributes, uint32_t* num_attributes,
                      const VkVertexInputBindingDescription** bindings, uint32_t* num_bindings,
                      int using_colors)
{
  GLOBAL VkVertexInputBindingDescription g_bindings[2] = {
    { 0, sizeof(Vertex_X3C), VK_VERTEX_INPUT_RATE_VERTEX },
    { 1, sizeof(Transform), VK_VERTEX_INPUT_RATE_INSTANCE }
  };
  GLOBAL VkVertexInputAttributeDescription g_attributes1[5] = {
    { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex_X3C, position) },
    { 1, 0, VK_FORMAT_R32_UINT, offsetof(Vertex_X3C, color) },
    { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Transform, rotation) },
    { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Transform, position) },
    { 4, 1, VK_FORMAT_R32_SFLOAT, offsetof(Transform, scale) },
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
