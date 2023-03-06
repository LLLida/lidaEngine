/*

  Simple packaging system.

 */

#define PACKAGE_MAGIC 22813376969420

typedef struct {

  Vec3 camera_position;
  Vec3 camera_up;
  Vec3 camera_rotation;

  uint32_t vox_components_offset;
  uint32_t num_vox_components;

} Scene_Info;

typedef struct {

  Transform transform;
  uint32_t palette[256];
  uint32_t w, h, d;

} Vox_Model_Serialized;

INTERNAL void
SaveState(const Camera* camera, const char* filename)
{
  void* file = PlatformOpenFileForWrite(filename);

  // write header
  uint64_t header = PACKAGE_MAGIC;
  PlatformWriteToFile(file, &header, sizeof(header));

  Scene_Info info = {
    .camera_position = camera->position,
    .camera_up       = camera->up,
    .camera_rotation = camera->rotation,
    .num_vox_components = ComponentCount(Voxel_Grid),
    .vox_components_offset = sizeof(Scene_Info) + sizeof(uint64_t)
  };
  PlatformWriteToFile(file, &info, sizeof(Scene_Info));

  EID* ids = ComponentIDs(Voxel_Grid);
  for (uint32_t i = 0; i < info.num_vox_components; i++) {
    EID entity = ids[i];
    Voxel_Grid* grid = GetComponent(Voxel_Grid, entity);
    Transform* transform = GetComponent(Transform, entity);

    PlatformWriteToFile(file, transform, sizeof(Transform));
    PlatformWriteToFile(file, grid->palette, sizeof(uint32_t)*256);
    PlatformWriteToFile(file, &grid->width, sizeof(uint32_t));
    PlatformWriteToFile(file, &grid->height, sizeof(uint32_t));
    PlatformWriteToFile(file, &grid->depth, sizeof(uint32_t));
    PlatformWriteToFile(file, grid->data->ptr, grid->width*grid->height*grid->depth);
  }

  PlatformCloseFileForWrite(file);
}

INTERNAL void
LoadState(ECS* ecs, Allocator* va, Camera* camera, const char* filename)
{
  size_t buffer_size;
  void* buffer = PlatformLoadEntireFile(filename, &buffer_size);
  if (buffer == NULL) {
    LOG_ERROR("failed to load package '%s'", filename);
    return;
  }

  if (*(uint64_t*)buffer == PACKAGE_MAGIC) {

    Scene_Info* info = (void*)((uint64_t*)buffer + 1);
    camera->position = info->camera_position;
    camera->up       = info->camera_up;
    camera->rotation = info->camera_rotation;

    Vox_Model_Serialized* model = (void*)((uint8_t*)buffer + info->vox_components_offset);
    for (uint32_t i = 0; i < info->num_vox_components; i++) {
      EID entity = CreateEntity(ecs);

      uint32_t offset = model->w*model->h*model->d;

      Voxel_Grid* vox = AddComponent(ecs, Voxel_Grid, entity);
      memcpy(vox->palette, model->palette, sizeof(uint32_t)*256);
      AllocateVoxelGrid(va, vox, model->w, model->h, model->d);
      memcpy(vox->data->ptr, model+1, offset);
      vox->hash = HashMemory64(vox->data->ptr, offset);

      Transform* transform = AddComponent(ecs, Transform, entity);
      memcpy(transform, &model->transform, sizeof(Transform));
      AddComponent(ecs, OBB, entity);

      model = (void*)((uint8_t*)(model + 1) + offset);
    }

  }
  PlatformFreeLoadedFile(buffer);
}
