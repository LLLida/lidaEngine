/*

  Simple packaging system.

 */

#define PACKAGE_MAGIC 22813376969420

// TODO: find a way to store entity relationships

typedef struct {

  Vec3 camera_position;
  Vec3 camera_up;
  Vec3 camera_rotation;

  uint32_t vox_grids_offset;
  uint32_t num_vox_grids;
  uint32_t vox_models_offset;
  uint32_t num_vox_models;

} Scene_Info;

typedef struct {

  Transform transform;
  uint32_t grid_id;
  int has_script;

} Vox_Model_Serialized;

typedef struct {

  char name[32];
  Script_Arg arg0;
  Script_Arg arg1;
  Script_Arg arg2;
  Script_Arg arg3;
  int frequency;

} Script_Serialized;

typedef struct {

  uint32_t palette[256];
  uint32_t w, h, d;

} Vox_Grid_Serialized;

INTERNAL void
SaveScene(const Camera* camera, const char* filename)
{
  void* file = PlatformOpenFileForWrite(filename);

  // write header
  const uint64_t header = PACKAGE_MAGIC;
  PlatformWriteToFile(file, &header, sizeof(header));

  Scene_Info info = {
    .camera_position = camera->position,
    .camera_up       = camera->up,
    .camera_rotation = camera->rotation,
    .num_vox_models = ComponentCount(Voxel_View),
    .vox_models_offset = sizeof(Scene_Info) + sizeof(uint64_t),
    .num_vox_grids = ComponentCount(Voxel_Grid),
    .vox_grids_offset = sizeof(Scene_Info) + sizeof(uint64_t)
  };
  FOREACH_COMPONENT(Voxel_View) {
    (void)components[i]; // HACK: avoid compiler warning
    Script* script = GetComponent(Script, entities[i]);
    info.vox_grids_offset += sizeof(Vox_Model_Serialized);
    if (script) {
      info.vox_grids_offset += sizeof(Script_Serialized);
    }
  }
  PlatformWriteToFile(file, &info, sizeof(Scene_Info));

  EID* ids = ComponentIDs(Voxel_View);
  for (uint32_t i = 0; i < info.num_vox_models; i++) {
    EID entity = ids[i];
    Voxel_View* vox = GetComponent(Voxel_View, entity);
    Transform* transform = GetComponent(Transform, entity);
    Script* script = GetComponent(Script, entity);
    int has_script = script ? 1 : 0;
    Voxel_Grid* grid = GetComponent(Voxel_Grid, vox->grid);
    Vox_Model_Serialized model = {
      .grid_id = grid - ComponentData(Voxel_Grid),
      .has_script = has_script
    };
    memcpy(&model.transform, transform, sizeof(Transform));
    PlatformWriteToFile(file, &model, sizeof(Vox_Model_Serialized));

    if (has_script) {
      Script_Serialized ss = {
        .arg0 = script->arg0,
        .arg1 = script->arg1,
        .arg2 = script->arg2,
        .arg3 = script->arg3,
        .frequency = script->frequency
      };
      strncpy(ss.name, script->name, sizeof(ss.name)-1);
      PlatformWriteToFile(file, &ss, sizeof(Script_Serialized));
    }
  }
  ids = ComponentIDs(Voxel_Grid);
  for (uint32_t i = 0; i < info.num_vox_grids; i++) {
    EID entity = ids[i];
    Voxel_Grid* grid = GetComponent(Voxel_Grid, entity);
    Vox_Grid_Serialized grid_info = {
      .w = grid->width,
      .h = grid->height,
      .d = grid->depth,
    };
    memcpy(grid_info.palette, grid->palette, sizeof(uint32_t)*256);
    PlatformWriteToFile(file, &grid_info, sizeof(Vox_Grid_Serialized));
    PlatformWriteToFile(file, grid->data->ptr, VoxelGridBytes(grid));
    // TODO: pad to 8 or 16 bytes
  }

  PlatformCloseFileForWrite(file);
  LOG_INFO("Saved current scene to file '%s'", filename);
}

INTERNAL void
LoadScene(ECS* ecs, Allocator* va, Camera* camera, Script_Manager* sm, const char* filename)
{
  size_t buffer_size;
  // TODO: packages might become very big, we should read directly
  // from file, i.e. without any buffers.
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

    EID* grid_ids = PersistentAllocate(sizeof(EID) * info->num_vox_grids);

    Vox_Model_Serialized* model = (void*)((uint8_t*)buffer + info->vox_models_offset);
    Vox_Grid_Serialized* grid = (void*)((uint8_t*)buffer + info->vox_grids_offset);

    // create entities for grids
    for (uint32_t i = 0; i < info->num_vox_grids; i++)
      grid_ids[i] = CreateEntity(ecs);
    // create Voxel_View's
    for (uint32_t i = 0; i < info->num_vox_models; i++) {
      EID entity = CreateEntity(ecs);

      Voxel_View* vox = AddComponent(ecs, Voxel_View, entity);
      vox->grid = grid_ids[model->grid_id];

      // load transform
      Transform* transform = AddComponent(ecs, Transform, entity);
      memcpy(transform, &model->transform, sizeof(Transform));
      AddComponent(ecs, OBB, entity);

      if (model->has_script) {
        // NOTE: we already have script in memory because 'scripts'
        // are functions in C. We just do a hash table lookup to
        // retrieve needed function pointers.
        Script_Serialized* info = (void*)((uint8_t*)(model + 1));
        Script* script = AddComponent(ecs, Script, entity);
        Script_Entry* entry = FHT_Search(&sm->scripts, GET_TYPE_INFO(Script_Entry),
                                         &(Script_Entry) { .name = info->name } );
        script->name = entry->name;
        script->func = entry->func;
        script->arg0 = info->arg0;
        script->arg1 = info->arg1;
        script->arg2 = info->arg2;
        script->arg3 = info->arg3;
        script->frequency = info->frequency;
        model = (void*)((uint8_t*)model + sizeof(Vox_Model_Serialized) + sizeof(Script));
      } else {
        model = (void*)((uint8_t*)model + sizeof(Vox_Model_Serialized));
      }
    }
    // load voxels
    for (uint32_t i = 0; i < info->num_vox_grids; i++) {
      Voxel_Grid* vox = AddComponent(ecs, Voxel_Grid, grid_ids[i]);
      // load palette
      memcpy(vox->palette, grid->palette, sizeof(uint32_t)*256);
      AllocateVoxelGrid(va, vox, grid->w, grid->h, grid->d);

      memcpy(vox->data->ptr, grid+1, VoxelGridBytes(vox));
      RehashVoxelGrid(vox);
      grid = (void*)((uint8_t*)(grid +1) + VoxelGridBytes(vox));
    }

    PersistentRelease(grid_ids);
  }
  PlatformFreeLoadedFile(buffer);
  LOG_INFO("Loaded scene from file '%s'", filename);
}
