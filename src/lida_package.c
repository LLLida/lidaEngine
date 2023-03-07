/*

  Simple packaging system.

 */

#define PACKAGE_MAGIC 22813376969420

// TODO: find a way to store entity relationships

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

INTERNAL void
SaveScene(const Camera* camera, const char* filename)
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
    Script* script = GetComponent(Script, entity);
    int has_script = script ? 1 : 0;

    Vox_Model_Serialized model = {
      .w = grid->width,
      .h = grid->height,
      .d = grid->depth,
      .has_script = has_script
    };
    memcpy(model.palette, grid->palette, sizeof(uint32_t)*256);
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
    PlatformWriteToFile(file, grid->data->ptr, grid->width*grid->height*grid->depth);
    // TODO: pad to 8 or 16 bytes
  }

  PlatformCloseFileForWrite(file);
}

INTERNAL void
LoadScene(ECS* ecs, Allocator* va, Camera* camera, Script_Manager* sm, const char* filename)
{
  size_t buffer_size;
  // TODO: packages might become very big, we should read directly from file
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

      uint32_t model_data_bytes = model->w*model->h*model->d;

      Voxel_Grid* vox = AddComponent(ecs, Voxel_Grid, entity);
      // load palette
      memcpy(vox->palette, model->palette, sizeof(uint32_t)*256);
      AllocateVoxelGrid(va, vox, model->w, model->h, model->d);

      // load transform
      Transform* transform = AddComponent(ecs, Transform, entity);
      memcpy(transform, &model->transform, sizeof(Transform));
      AddComponent(ecs, OBB, entity);

      // 'load' script if this component has one
      void* model_data = model+1;
      if (model->has_script) {
        // NOTE: we already have script in memory because 'scripts'
        // are functions in C. We just do a hash table lookup to
        // retrieve needed pointers.
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

        model_data = (uint8_t*)model + sizeof(Vox_Model_Serialized) + sizeof(Script_Serialized);
      }

      // load voxel data
      memcpy(vox->data->ptr, model_data, model_data_bytes);
      vox->hash = HashMemory64(vox->data->ptr, model_data_bytes);

      model = (void*)((uint8_t*)model_data + model_data_bytes);
    }

  }
  PlatformFreeLoadedFile(buffer);
}
