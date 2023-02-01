#include "voxel.h"
#include "memory.h"

#include "SDL_rwops.h"

#define OGT_VOX_IMPLEMENTATION
#include "ogt_vox.h"

// TODO: don't use this
#include <algorithm> // for std::sort



typedef struct {

  // this is for vkCmdDraw
  uint32_t vertexCount;
  uint32_t firstVertex;
  uint32_t firstInstance;
  uint32_t instanceCount;
  // this is for culling
  // vec3 normalVector;
  // vec3 position;

} DrawCommand;

typedef struct {

  uint64_t hash;
  uint32_t first_vertex;
  uint32_t last_vertex;
  uint32_t first_draw_id;

} MeshInfo;

typedef struct {
  uint32_t draw_id;
} DrawID;

typedef struct {
  uint32_t draw_id;
  // counter for robin-hood hashing
  uint32_t psl;
} DrawHash;

lida_TypeInfo g_draw_command_type_info;
lida_TypeInfo g_mesh_type_info;
lida_TypeInfo g_draw_id_type_info;
lida_TypeInfo g_draw_hash_type_info;

static lida_Voxel GridGetChecked(const lida_VoxelGrid* grid, uint32_t x, uint32_t y, uint32_t z);
static void DrawerUpdateCache(lida_VoxelDrawer* drawer);
static const uint32_t* FindDrawByHash(lida_VoxelDrawer* drawer, uint64_t hash);
static DrawID* FindNearestDraw(lida_VoxelDrawer* drawer, uint32_t offset);


/// voxel grid

int
lida_VoxelGridAllocate(lida_VoxelGrid* grid, uint32_t w, uint32_t h, uint32_t d)
{
  grid->data = NULL;
  return lida_VoxelGridReallocate(grid, w, h, d);
}

int
lida_VoxelGridReallocate(lida_VoxelGrid* grid, uint32_t w, uint32_t h, uint32_t d)
{
  lida_Voxel* old_data = grid->data;
  grid->data = (lida_Voxel*)lida_Malloc(w*h*d);
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

void lida_VoxelGridFreeWrapper(void* grid)
{
  lida_VoxelGridFree((lida_VoxelGrid*)grid);
}

void
lida_VoxelGridSet(lida_VoxelGrid* grid, uint32_t x, uint32_t y, uint32_t z, lida_Voxel vox)
{
  lida_VoxelGridGet(grid, x, y, z) = vox;
  uint64_t hashes[2] = { grid->hash, (uint64_t)vox };
  grid->hash = lida_HashCombine64(hashes, 2);
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
lida_VoxelGridMaxGeneratedVertices(const lida_VoxelGrid* grid)
{
  // I made some conclusions and I certain that at most half voxels are written in worst case.
  // However, we need a mathematical proof.
  return 3 * grid->width * grid->height * grid->depth;
}

uint32_t
lida_VoxelGridGenerateMeshNaive(const lida_VoxelGrid* grid, lida_VoxelVertex* vertices, int face)
{
  lida_VoxelVertex* begin = vertices;
  lida_Vec3 half_size = {
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
        lida_Voxel voxel = lida_VoxelGridGet(grid, x, y, z);
        lida_Voxel near_voxel;
        if (x+offsetX < grid->width &&
            y+offsetY < grid->height &&
            z+offsetZ < grid->depth) {
          near_voxel = lida_VoxelGridGet(grid, x+offsetX, y+offsetY, z+offsetZ);
        } else {
          near_voxel = 0;
        }
        if (// check if voxel is not air
            voxel &&
            // check if near voxel is air
            near_voxel == 0) {
          lida_Vec3 pos = lida_Vec3{(float)x, (float)y, (float)z} - half_size;
          // TODO: use index buffers
          for (uint32_t i = 0; i < 6; i++) {
            vertices[i].position = pos + vox_positions[face*6 + i];
            vertices[i].color = grid->palette[voxel];
          }
          vertices += 6;
        }
      }
  LIDA_LOG_DEBUG("wrote %u vertices", uint32_t(vertices - begin));
  return vertices - begin;
}

uint32_t
lida_VoxelGridGenerateMeshGreedy(const lida_VoxelGrid* grid, lida_VoxelVertex* vertices, int face)
{
  lida_VoxelVertex* begin = vertices;
  lida_Vec3 half_size = {
    grid->width * 0.5f,
    grid->height * 0.5f,
    grid->depth * 0.5f
  };
  const uint32_t dims[3] = { grid->width, grid->height, grid->depth };
  const int d = face >> 1;
  const int u = (d+1)%3, v = (d+2)%3;
  char* merged_mask = (char*)lida_TempAllocate(dims[u]*dims[v]);
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
        lida_Voxel start_voxel = lida_VoxelGridGet(grid, pos[0], pos[1], pos[2]);
        if (start_voxel == 0) {
          // we don't generate vertices for air
          continue;
        }
        const uint32_t start_pos[3] = { pos[0], pos[1], pos[2] };
        uint32_t min_i = dims[u];
        // grow quad while all voxels in that quad are the same
        while (pos[v] < dims[v]) {
          pos[u] = i;
          if (lida_VoxelGridGet(grid, pos[0], pos[1], pos[2]) != start_voxel)
            break;
          pos[u]++;
          while (pos[u] < min_i &&
                 lida_VoxelGridGet(grid, pos[0], pos[1], pos[2]) == start_voxel) {
            pos[u]++;
          }
          min_i = std::min(min_i, pos[u]);
          pos[v]++;
        }
        uint32_t offset[3] = { 0 };
        offset[u] = min_i - start_pos[u]; // width of quad
        offset[v] = pos[v] - start_pos[v]; // height of quad
        int p[3] = { 0 };
        // check if at least 1 voxel is visible. FIXME: I think when this approach generates the most perfect meshes,
        // it generates ugly meshes: when camera is inside a mesh some unnecessary voxels are seen.
        for (p[v] = start_pos[v], p[d] = layer; (uint32_t)p[v] < start_pos[v] + offset[v]; p[v]++) {
          for (p[u] = start_pos[u]; (uint32_t)p[u] < start_pos[u] + offset[u]; p[u]++) {
            lida_Voxel near_voxel = GridGetChecked(grid,
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
          vertices[vert_index].position = lida_Vec3{(float)vert_pos[0], (float)vert_pos[1], (float)vert_pos[2]} - half_size;
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
  lida_TempFree(merged_mask);
  LIDA_LOG_DEBUG("wrote %u vertices", uint32_t(vertices - begin));
  return vertices - begin;
}

int
lida_VoxelGridLoad(lida_VoxelGrid* grid, const uint8_t* buffer, uint32_t size)
{
  LIDA_PROFILE_FUNCTION();
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
  grid->hash = lida_HashMemory64(grid->data, grid->width * grid->height * grid->depth);
  return 0;
}

int
lida_VoxelGridLoadFromFile(lida_VoxelGrid* grid, const char* filename)
{
  LIDA_PROFILE_FUNCTION();
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


/// Voxel drawer

VkResult
lida_VoxelDrawerCreate(lida_VoxelDrawer* drawer, uint32_t max_vertices, uint32_t max_draws)
{
  LIDA_PROFILE_FUNCTION();
  // empty-initialize containers
  g_draw_command_type_info = LIDA_TYPE_INFO(DrawCommand, lida_MallocAllocator(), 0);
  g_mesh_type_info = LIDA_TYPE_INFO(MeshInfo, lida_MallocAllocator(), 0);
  g_draw_id_type_info = LIDA_TYPE_INFO(DrawID, lida_MallocAllocator(), 0);
  g_draw_hash_type_info = LIDA_TYPE_INFO(DrawHash, lida_MallocAllocator(), 0);
  for (int i = 0; i < 2; i++) {
    drawer->frames[i].draws = lida::dyn_array_empty(&g_draw_command_type_info);
    drawer->frames[i].meshes = lida::dyn_array_empty(&g_mesh_type_info);
    lida_DynArrayReserve(&drawer->frames[i].meshes, 32);
    lida_DynArrayReserve(&drawer->frames[i].draws, drawer->frames[i].meshes.size * 6);
  }
  drawer->hashes_cached = lida::dyn_array_empty(&g_draw_hash_type_info);
  drawer->regions_cached = lida::dyn_array_empty(&g_draw_id_type_info);
  // create buffers
  VkResult err = lida_BufferCreate(&drawer->vertex_buffer, max_vertices * sizeof(lida_VoxelVertex),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/vertex-buffer");
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create vertex buffer for storing voxel meshes with error %s",
                   lida_VkResultToString(err));
    return err;
  }
  err = lida_BufferCreate(&drawer->transform_buffer, max_draws * sizeof(lida_Transform),
                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/storage-buffer");
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("faled to create storage buffer for storing voxel transforms with error %s",
                   lida_VkResultToString(err));
    return err;
  }
  VkMemoryRequirements buffer_requirements[2];
  vkGetBufferMemoryRequirements(lida_GetLogicalDevice(),
                                drawer->vertex_buffer, &buffer_requirements[0]);
  vkGetBufferMemoryRequirements(lida_GetLogicalDevice(),
                                drawer->transform_buffer, &buffer_requirements[1]);
  VkMemoryRequirements requirements;
  lida_MergeMemoryRequirements(buffer_requirements, LIDA_ARR_SIZE(buffer_requirements), &requirements);
  // try to allocate device local memory accessible from CPU
  VkMemoryPropertyFlags required_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  err = lida_VideoMemoryAllocate(&drawer->memory, requirements.size,
                                 required_flags|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 requirements.memoryTypeBits,
                                 "voxel-drawer/memory");
  if (err != VK_SUCCESS) {
    // if failed try to allocate any memory accessible from CPU
    err = lida_VideoMemoryAllocate(&drawer->memory, requirements.size,
                                   required_flags,
                                   requirements.memoryTypeBits,
                                   "voxel-drawer/memory");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to allocate video memory for voxels with error %s", lida_VkResultToString(err));
      return err;
    }
  }
  // bind buffers to allocated memory
  err = lida_BufferBindToMemory(&drawer->memory, drawer->vertex_buffer,
                                &buffer_requirements[0], (void**)&drawer->pVertices, NULL);
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to bind vertex buffer to memory with error %s", lida_VkResultToString(err));
  }
  err = lida_BufferBindToMemory(&drawer->memory, drawer->transform_buffer,
                                &buffer_requirements[1], (void**)&drawer->pTransforms, NULL);
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to bind storage buffer to memory with error %s", lida_VkResultToString(err));
  }
  // allocate vertex temp buffer
  drawer->vertex_temp_buffer_size = 20 * 1024;
  drawer->vertex_temp_buffer = (lida_VoxelVertex*)lida_Malloc(drawer->vertex_temp_buffer_size * sizeof(lida_VoxelVertex));
  //
  drawer->max_draws = max_draws;
  drawer->max_vertices = max_vertices;
  drawer->vertex_offset = 0;
  drawer->frame_id = 1;
  return err;
}

void
lida_VoxelDrawerDestroy(lida_VoxelDrawer* drawer)
{
  LIDA_PROFILE_FUNCTION();
  lida_MallocFree(drawer->vertex_temp_buffer);
  vkDestroyBuffer(lida_GetLogicalDevice(), drawer->transform_buffer, NULL);
  vkDestroyBuffer(lida_GetLogicalDevice(), drawer->vertex_buffer, NULL);
  lida_VideoMemoryFree(&drawer->memory);
}

void
lida_VoxelDrawerNewFrame(lida_VoxelDrawer* drawer)
{
  LIDA_PROFILE_FUNCTION();
  drawer->frame_id = 1 - drawer->frame_id;
  if (drawer->frame_id == 0)
    drawer->transform_offset = 0;
  lida_DynArrayClear(&drawer->frames[drawer->frame_id].draws);
  lida_DynArrayClear(&drawer->frames[drawer->frame_id].meshes);
  DrawerUpdateCache(drawer);
}

void
lida_VoxelDrawerPushMesh(lida_VoxelDrawer* drawer, const lida_VoxelGrid* grid, const lida_Transform* transform)
{
  LIDA_PROFILE_FUNCTION();
  uint32_t index = drawer->frames[drawer->frame_id].meshes.size;
  // add transform
  memcpy(&drawer->pTransforms[drawer->transform_offset], transform, sizeof(lida_Transform));
  if (index > 0) {
    // try to use instancing
    auto prev_mesh = lida::get<MeshInfo>(&drawer->frames[drawer->frame_id].meshes, index-1);
    if (prev_mesh->hash == grid->hash) {
      auto draws = (DrawCommand*)drawer->frames[drawer->frame_id].draws.ptr;
      for (uint32_t i = 0; i < 6; i++) {
        draws[prev_mesh->first_draw_id+i].instanceCount++;
      }
      drawer->transform_offset++;
      return;
    }
  }
  const uint32_t* draw_id = FindDrawByHash(drawer, grid->hash);
  auto prev_frame = &drawer->frames[1-drawer->frame_id];
  auto prev_meshes = (MeshInfo*)prev_frame->meshes.ptr;
  auto mesh = lida::push_back<MeshInfo>(&drawer->frames[drawer->frame_id].meshes);
  if (draw_id) {
    // if we found that a grid with exact same hash was rendered in previous frame,
    // then use previous frame's vertices in this frame.
    // This helps to not wasting time generating vertices every frame.
    auto prev_draws = (DrawCommand*)prev_frame->draws.ptr;
    memcpy(mesh, &prev_meshes[*draw_id], sizeof(MeshInfo));
    for (int i = 0; i < 6; i++) {
      auto dst = lida::push_back<DrawCommand>(&drawer->frames[drawer->frame_id].draws);
      auto src = &prev_draws[prev_meshes[*draw_id].first_draw_id + i];
      dst->firstVertex = src->firstVertex;
      dst->vertexCount = src->vertexCount;
      dst->firstInstance = drawer->transform_offset;
      dst->instanceCount = 1;
    }
  } else {
    // if hash not found then generate new vertices and draw data.
    mesh->first_vertex = drawer->vertex_offset;
    // upper_bound is first vertex position which will be untouched in worst case scenario
    uint32_t upper_bound = drawer->vertex_offset + lida_VoxelGridMaxGeneratedVertices(grid);
    DrawID* d = FindNearestDraw(drawer, drawer->vertex_offset);
    for (int i = 0; i < 6; i++) {
      auto command = lida::push_back<DrawCommand>(&drawer->frames[drawer->frame_id].draws);
      if (upper_bound > prev_meshes[d->draw_id].first_vertex) {
        // if we're not sure if we can write voxels safely at this position
        // then write to a temporary buffer and see if we can fit to current free region
        // command->vertexCount = lida_VoxelGridGenerateMeshNaive(grid, scale,
        command->vertexCount = lida_VoxelGridGenerateMeshGreedy(grid, drawer->vertex_temp_buffer, i);
        while (drawer->vertex_offset + command->vertexCount > prev_meshes[d->draw_id].first_vertex) {
          drawer->vertex_offset = prev_meshes[d->draw_id].last_vertex;
          d = FindNearestDraw(drawer, drawer->vertex_offset);
        }
        // copy temporary buffer to newly found free region
        memcpy(drawer->pVertices + drawer->vertex_offset,
               drawer->vertex_temp_buffer,
               command->vertexCount * sizeof(lida_VoxelVertex));
      } else {
        // if there's enough space we can write vertices directly to buffer
        // command->vertexCount = lida_VoxelGridGenerateMeshNaive(grid, scale,
        command->vertexCount =
          lida_VoxelGridGenerateMeshGreedy(grid, drawer->pVertices + drawer->vertex_offset, i);
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

void
lida_VoxelDrawerDraw(lida_VoxelDrawer* drawer, VkCommandBuffer cmd)
{
  LIDA_PROFILE_FUNCTION();
  VkDeviceSize offsets[] = { 0, 0 };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer };
  vkCmdBindVertexBuffers(cmd, 0, LIDA_ARR_SIZE(buffers), buffers, offsets);
  lida_DynArray* draws = &drawer->frames[drawer->frame_id].draws;
  for (uint32_t i = 0; i < draws->size; i += 6) {
    DrawCommand* command = LIDA_DA_GET(draws, DrawCommand, i);
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

void
lida_VoxelDrawerDrawWithNormals(lida_VoxelDrawer* drawer, VkCommandBuffer cmd, uint32_t normal_id)
{
  LIDA_PROFILE_FUNCTION();
  VkDeviceSize offsets[] = { 0, 0 };
  VkBuffer buffers[] = { drawer->vertex_buffer, drawer->transform_buffer };
  vkCmdBindVertexBuffers(cmd, 0, LIDA_ARR_SIZE(buffers), buffers, offsets);
  lida_DynArray* draws = &drawer->frames[drawer->frame_id].draws;
  for (uint32_t i = normal_id; i < draws->size; i += 6) {
    DrawCommand* command = LIDA_DA_GET(draws, DrawCommand, i);
    vkCmdDraw(cmd,
              command->vertexCount, command->instanceCount,
              command->firstVertex, command->firstInstance);
  }
}

void
lida_PipelineVoxelVertices(const VkVertexInputAttributeDescription** attributes, uint32_t* num_attributes,
                           const VkVertexInputBindingDescription** bindings, uint32_t* num_bindings)
{
  static VkVertexInputBindingDescription g_bindings[2];
  static VkVertexInputAttributeDescription g_attributes[5];
  g_bindings[0] = { 0, sizeof(lida_VoxelVertex), VK_VERTEX_INPUT_RATE_VERTEX };
  g_bindings[1] = { 1, sizeof(lida_Transform), VK_VERTEX_INPUT_RATE_INSTANCE };
  g_attributes[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(lida_VoxelVertex, position) };
  g_attributes[1] = { 1, 0, VK_FORMAT_R32_UINT, offsetof(lida_VoxelVertex, color) };
  g_attributes[2] = { 2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(lida_Transform, rotation) };
  g_attributes[3] = { 3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(lida_Transform, position) };
  g_attributes[4] = { 4, 1, VK_FORMAT_R32_SFLOAT, offsetof(lida_Transform, scale) };
  *attributes = g_attributes;
  *bindings = g_bindings;
  *num_attributes = LIDA_ARR_SIZE(g_attributes);
  *num_bindings = LIDA_ARR_SIZE(g_bindings);
}



lida_Voxel
GridGetChecked(const lida_VoxelGrid* grid, uint32_t x, uint32_t y, uint32_t z)
{
  if (x < grid->width &&
      y < grid->height &&
      z < grid->depth) {
    return lida_VoxelGridGet(grid, x, y, z);
  }
  return 0;
}

void
DrawerUpdateCache(lida_VoxelDrawer* drawer)
{
  // https://stackoverflow.com/questions/1193477/fast-algorithm-to-quickly-find-the-range-a-number-belongs-to-in-a-set-of-ranges
  auto prev_meshes = &drawer->frames[1-drawer->frame_id].meshes;
  uint32_t n = prev_meshes->size;
  auto meshes = (MeshInfo*)prev_meshes->ptr;
  const auto n2 = lida_NearestPow2(n);
  lida_DynArrayResize(&drawer->hashes_cached, n2);
  lida_DynArrayResize(&drawer->regions_cached, n+1);
  // udpate hashes_cached with insertion sort
  auto hashes = (DrawHash*)drawer->hashes_cached.ptr;
  auto regions = (DrawID*)drawer->regions_cached.ptr;
  // TODO: use better algorithm like quick sort or heap sort
  for (uint32_t  i = 0; i < n; i++) {
    regions[i].draw_id = i;
  }
  for (uint32_t i = 0; i < n2; i++) {
    hashes[i].draw_id = UINT32_MAX;
  }
  // build hash table of draws from previous frame using robin-hood hashing
  // https://programming.guide/robin-hood-hashing.html
  for (uint32_t i = 0; i < n; i++) {
    MeshInfo* mesh = &meshes[i];
    // because n2 is a power of 2, we can do '&' instead of expensive '%'
    uint32_t id = mesh->hash & (n2-1);
    DrawHash obj;
    obj.draw_id = i;
    obj.psl = 0;
    while (hashes[id].draw_id != UINT32_MAX) {
      if (obj.psl > hashes[id].psl) {
        std::swap(obj, hashes[id]);
      }
      obj.psl++;
      id = (id+1) & (n2-1);
    }
    hashes[id] = obj;
  }
  // I hope in future we will replace this garbage with a proper C code
  std::sort(regions, regions + n, [meshes] (const DrawID& lhs, const DrawID& rhs) {
    return meshes[lhs.draw_id].first_vertex < meshes[rhs.draw_id].first_vertex;
  });
  // upper bound of buffer
  regions[n].draw_id = n;
  auto border = lida::push_back<MeshInfo>(prev_meshes);
  border->first_vertex = drawer->max_vertices;
  border->last_vertex = 0;
}

const uint32_t*
FindDrawByHash(lida_VoxelDrawer* drawer, uint64_t hash)
{
  uint32_t n = drawer->hashes_cached.size;
  if (n > 0) {
    // n is always power of two
    DrawHash* hashes = (DrawHash*)drawer->hashes_cached.ptr;
    auto prev_meshes = (MeshInfo*)drawer->frames[1-drawer->frame_id].meshes.ptr;
    uint32_t id = hash & (n-1);
    uint32_t psl = 0;
    while (true) {
      if (hashes[id].draw_id == UINT32_MAX) {
        // empty slot found => return NULL
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

DrawID*
FindNearestDraw(lida_VoxelDrawer* drawer, uint32_t offset)
{
  // based on https://en.cppreference.com/w/cpp/algorithm/upper_bound
  uint32_t n = drawer->regions_cached.size;
  auto regions = (DrawID*)drawer->regions_cached.ptr;
  auto ptr = regions;
  auto prev_meshes = (MeshInfo*)drawer->frames[1-drawer->frame_id].meshes.ptr;
  while (n > 0) {
    uint32_t step = n / 2;
    auto it = ptr + step;
    if (offset >= prev_meshes[it->draw_id].first_vertex) {
      ptr = it+1;
      n -= step + 1;
    } else {
      n = step;
    }
  }
  return ptr;
}
