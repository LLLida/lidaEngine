#include "voxel.h"
#include "linalg.h"
#include "memory.h"

#include "SDL_rwops.h"

#define OGT_VOX_IMPLEMENTATION
#include "ogt_vox.h"



typedef struct {
  uint32_t draw_id;
} DrawID;

static int CompareDrawCommands(const void* l, const void* r);
static lida_VoxelDrawCommand* BinSearch(lida_VoxelDrawCommand* array, uint32_t count, uint64_t hash);
static void DrawerUpdateCache(lida_VoxelDrawer* drawer);



int
lida_VoxelGridAllocate(lida_VoxelGrid* grid, uint32_t w, uint32_t h, uint32_t d)
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
lida_VoxelGridGenerateMeshNaive(const lida_VoxelGrid* grid, float scale, lida_VoxelVertex* vertices, int face)
{
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
          lida_Vec3 pos = lida_Vec3{(float)x, (float)y, (float)z} * scale - half_size;
          // TODO: use index buffers
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
lida_VoxelGridGenerateMeshGreedy(const lida_VoxelGrid* grid, lida_VoxelVertex* vertices)
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
  grid->hash = lida_HashMemory64(grid->data, grid->width * grid->height * grid->depth);
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

VkResult lida_VoxelDrawerCreate(lida_VoxelDrawer* drawer, uint32_t max_vertices, uint32_t max_draws)
{
  // empty-initialize containers
  drawer->draw_command_type_info = LIDA_TYPE_INFO(lida_VoxelDrawCommand, lida_MallocAllocator(), 0);
  drawer->draw_id_type_info = LIDA_TYPE_INFO(DrawID, lida_MallocAllocator(), 0);
  for (int i = 0; i < 2; i++) {
    drawer->frames[i].draws = lida::dyn_array_empty(&drawer->draw_command_type_info);
    lida_DynArrayReserve(&drawer->frames[i].draws, 32);
  }
  drawer->hashes_cached = lida::dyn_array_empty(&drawer->draw_id_type_info);
  drawer->regions_cached = lida::dyn_array_empty(&drawer->draw_id_type_info);
  // create buffers
  VkResult err = lida_BufferCreate(&drawer->vertex_buffer, max_vertices * sizeof(lida_VoxelVertex),
                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "voxel-drawer/vertex-buffer");
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("failed to create vertex buffer for storing voxel meshes with error %s",
                   lida_VkResultToString(err));
    return err;
  }
  err = lida_BufferCreate(&drawer->storage_buffer, max_draws * sizeof(lida_Transform),
                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "voxel-drawer/storage-buffer");
  if (err != VK_SUCCESS) {
    LIDA_LOG_ERROR("faled to create storage buffer for storing voxel transforms with error %s",
                   lida_VkResultToString(err));
    return err;
  }
  VkMemoryRequirements buffer_requirements[2];
  vkGetBufferMemoryRequirements(lida_GetLogicalDevice(),
                                drawer->vertex_buffer, &buffer_requirements[0]);
  vkGetBufferMemoryRequirements(lida_GetLogicalDevice(),
                                drawer->storage_buffer, &buffer_requirements[1]);
  VkMemoryRequirements requirements;
  lida_MergeMemoryRequirements(buffer_requirements, LIDA_ARR_SIZE(buffer_requirements), &requirements);
  // try to allocate device local memory accessible from CPU
  err = lida_VideoMemoryAllocate(&drawer->memory, requirements.size,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                 requirements.memoryTypeBits,
                                 "voxel-drawer/memory");
  if (err != VK_SUCCESS) {
    // if failed try to allocate any memory accessible from CPU
    err = lida_VideoMemoryAllocate(&drawer->memory, requirements.size,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                                 requirements.memoryTypeBits,
                                 "voxel-drawer/memory");
    if (err != VK_SUCCESS) {
      LIDA_LOG_ERROR("failed to allocate video memory for voxels with error %s", lida_VkResultToString(err));
      return err;
    }
  }
  // bind buffers to allocated memory
  err = lida_BufferBindToMemory(&drawer->memory, drawer->vertex_buffer,
                                &buffer_requirements[0], (void**)&drawer->pVertices, &drawer->mapped_ranges[0]);
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to bind vertex buffer to memory with error %s", lida_VkResultToString(err));
  }
  err = lida_BufferBindToMemory(&drawer->memory, drawer->storage_buffer,
                                &buffer_requirements[1], (void**)&drawer->pTransforms, &drawer->mapped_ranges[1]);
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to bind storage buffer to memory with error %s", lida_VkResultToString(err));
  }
  // allocate and update descriptor set
  auto binding = lida::descriptor_binding_info(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                               VK_SHADER_STAGE_VERTEX_BIT,
                                               drawer->storage_buffer, 0, VK_WHOLE_SIZE);
  lida_AllocateAndUpdateDescriptorSet(&binding, 1, &drawer->descriptor_set, 0, "voxel-storage-buffer");
  // init data needed for pipeline creation
  drawer->vertex_binding = { 0, sizeof(lida_VoxelVertex), VK_VERTEX_INPUT_RATE_VERTEX };
  drawer->vertex_attributes[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
  drawer->vertex_attributes[1] = { 1, 0, VK_FORMAT_R32_UINT, sizeof(lida_Vec3) };
  //
  drawer->max_draws = max_draws;
  drawer->max_vertices = max_vertices;
  drawer->frame_id = 1;
  return err;
}

void
lida_VoxelDrawerDestroy(lida_VoxelDrawer* drawer)
{
  vkDestroyBuffer(lida_GetLogicalDevice(), drawer->storage_buffer, NULL);
  vkDestroyBuffer(lida_GetLogicalDevice(), drawer->vertex_buffer, NULL);
  lida_VideoMemoryFree(&drawer->memory);
}

void
lida_VoxelDrawerNewFrame(lida_VoxelDrawer* drawer)
{
  drawer->frame_id = 1 - drawer->frame_id;
  drawer->vertex_offset = drawer->frame_id * drawer->max_vertices / 2;
  drawer->frames[drawer->frame_id].num_vertices = 0;
  lida_DynArrayClear(&drawer->frames[drawer->frame_id].draws);
  // if (prev->size > 0) {
  //   DrawerUpdateCache(drawer);
  // }
}

void
lida_VoxelDrawerFlushMemory(lida_VoxelDrawer* drawer)
{
  VkMappedMemoryRange mapped_ranges[2];
  memcpy(mapped_ranges, drawer->mapped_ranges, sizeof(mapped_ranges));
  mapped_ranges[0].size = drawer->frames[drawer->frame_id].num_vertices * sizeof(lida_VoxelVertex);
  mapped_ranges[1].size = drawer->frames[drawer->frame_id].draws.size / 6 * sizeof(lida_Transform);
  for (uint32_t i = 0; i < LIDA_ARR_SIZE(mapped_ranges); i++) {
    mapped_ranges[i].size = LIDA_ALIGN_TO(mapped_ranges[i].size, lida_GetDeviceProperties()->limits.nonCoherentAtomSize);
  }
  VkResult err = vkFlushMappedMemoryRanges(lida_GetLogicalDevice(),
                                           LIDA_ARR_SIZE(mapped_ranges), mapped_ranges);
  if (err != VK_SUCCESS) {
    LIDA_LOG_WARN("failed to flush memory for voxels with error %s", lida_VkResultToString(err));
  }
}

void
lida_VoxelDrawerPushMesh(lida_VoxelDrawer* drawer, float scale, const lida_VoxelGrid* grid, const lida_Transform* transform)
{
  uint32_t* num_vertices = &drawer->frames[drawer->frame_id].num_vertices;
  // uint32_t old_num_vertices = *num_vertices;
  uint32_t index = drawer->frames[drawer->frame_id].draws.size;
  for (int i = 0; i < 6; i++) {
    auto command = (lida_VoxelDrawCommand*)lida_DynArrayPushBack(&drawer->frames[drawer->frame_id].draws);
    command->vertexCount = lida_VoxelGridGenerateMeshNaive(grid, scale,
                                                           drawer->pVertices + drawer->vertex_offset + *num_vertices, i);
    command->firstVertex = *num_vertices;
    command->firstInstance = (index << 3) | i;
    *num_vertices += command->vertexCount;
  }
  memcpy(&drawer->pTransforms[index], transform, sizeof(lida_Transform));
}

void
lida_VoxelDrawerDraw(lida_VoxelDrawer* drawer, VkCommandBuffer cmd)
{
  VkDeviceSize offset = 0;
  vkCmdBindVertexBuffers(cmd, 0, 1, &drawer->vertex_buffer, &offset);
  lida_DynArray* draws = &drawer->frames[drawer->frame_id].draws;
  for (uint32_t i = 0; i < draws->size; i++) {
    lida_VoxelDrawCommand* command = LIDA_DA_GET(draws, lida_VoxelDrawCommand, i);
    vkCmdDraw(cmd, command->vertexCount, 1, command->firstVertex, command->firstInstance);
  }
}



int
CompareDrawCommands(const void* l, const void* r)
{
  auto lhs = (const lida_VoxelDrawCommand*)l;
  auto rhs = (const lida_VoxelDrawCommand*)r;
  return (lhs->hash > rhs->hash) - (lhs->hash < rhs->hash);
}

lida_VoxelDrawCommand*
BinSearch(lida_VoxelDrawCommand* array, uint32_t count, uint64_t hash)
{
  uint32_t left = 0, right = count;
  while (left != right) {
    uint32_t m = (left + right) / 2;
    lida_VoxelDrawCommand* mid = &array[m];
    if (mid->hash == hash) {
      return mid;
    } else if (mid->hash < hash) {
      right = m;
    } else {
      left = m;
    }
  }
  return NULL;
}

void
DrawerUpdateCache(lida_VoxelDrawer* drawer)
{
  // https://stackoverflow.com/questions/1193477/fast-algorithm-to-quickly-find-the-range-a-number-belongs-to-in-a-set-of-ranges
  lida_DynArray* prev = &drawer->frames[1-drawer->frame_id].draws;
  lida_DynArrayResize(&drawer->hashes_cached, prev->size);
  // udpate hashes_cached with insertion sort
  auto hashes = (DrawID*)drawer->hashes_cached.ptr;
  hashes->draw_id = 0;
  for (int i = 1; i < (int)prev->size; i++) {
    int j = i-1;
    auto r = lida::get<lida_VoxelDrawCommand>(prev,
                                              hashes[i].draw_id);
    while (j >= 0 && r->hash <= lida::get<lida_VoxelDrawCommand>(prev, j)->hash) {
      hashes[j+1] = hashes[j];
      j--;
    }
    hashes[j+1] = hashes[i];
  }
  lida_DynArrayResize(&drawer->regions_cached, prev->size);
}
