/*

  lida engine asset manager.

 */

typedef void(*Asset_Reload_Func)(void* component, const char* path, void* data);

typedef struct {

  const char* name;
  EID id;
  const Sparse_Set* set;
  Asset_Reload_Func reload_func;
  void* udata;

} Asset_ID;

// this just maps asset names to entity ids
typedef struct {

  Fixed_Hash_Table asset_ids;
  Type_Info asset_id_type_info;

} Asset_Manager;

typedef void(*Pipeline_Create_Func)(Pipeline_Desc* description);

typedef struct {

  VkPipeline pipeline;
  VkPipelineLayout layout;
  // this is only accessed when compiling pipeline
  Pipeline_Create_Func create_func;
  const char* vertex_shader;
  const char* fragment_shader;

} Pipeline_Program;
DECLARE_COMPONENT(Pipeline_Program);


/// private functions

INTERNAL uint32_t
HashAssetID(const void* obj)
{
  const Asset_ID* asset = obj;
  return HashString32(asset->name);
}

INTERNAL int
CompareAssetID(const void* lhs, const void* rhs)
{
  const Asset_ID* l = lhs, *r = rhs;
  return strcmp(l->name, r->name);
}

INTERNAL void
VoxelGrid_ReloadFunc(void* component, const char* path, void* data)
{
  Voxel_Grid* vox = component;
  Allocator* allocator = data;
  FreeVoxelGrid(allocator, vox);
  LoadVoxelGridFromFile(allocator, vox, path);
}

INTERNAL void
PipelineProgram_ReloadFunc(void* component, const char* path, void* data)
{
  (void)path;
  Pipeline_Program* program = component;
  VkPipeline old_pipeline = program->pipeline;
  Pipeline_Desc desc;
  program->create_func(&desc);
  desc.vertex_shader = program->vertex_shader;
  desc.fragment_shader = program->fragment_shader;
  ForceUpdateShader(path);
  VkResult err = CreateGraphicsPipelines(&program->pipeline, 1, &desc, &program->layout);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to recreate pipeline with error %s", ToString_VkResult(err));
    program->pipeline = old_pipeline;
  } else {
    // add old pipeline to deletion queue
    Deletion_Queue* dq = data;
    AddForDeletion(dq, (uint64_t)old_pipeline, VK_OBJECT_TYPE_PIPELINE);
  }
}

// INTERNAL void
// Font_ReloadFunc(void* component, const char* path, void* data)
// {
//   // NOTE: we don't reuse font atlas space. We may easily run out of space.
//   Font* font = component;
//   Font_Atlas* atlas = font->udata;
//   Bitmap_Renderer* renderer = data;
//   LoadToFontAtlas(renderer, atlas, cmd, font, path, font->pixel_size);
// }


/// public functions

INTERNAL void
InitAssetManager(Asset_Manager* am)
{
  const size_t num_assets = 256;
  am->asset_id_type_info = TYPE_INFO(Asset_ID, &HashAssetID, &CompareAssetID);
  FHT_Init(&am->asset_ids,
           PersistentAllocate(FHT_CALC_SIZE(&am->asset_id_type_info, num_assets)),
           num_assets, &am->asset_id_type_info);
}

INTERNAL void
FreeAssetManager(Asset_Manager* am, int free_memory)
{
  if (free_memory) {
    PersistentRelease(am->asset_ids.ptr);
  }
}

// return entity id of asset that has tag 'name'
// (EID)-1 returned if not found
INTERNAL EID
GetAssetByName(Asset_Manager* am, const char* name)
{
  Asset_ID* asset = FHT_Search(&am->asset_ids, &am->asset_id_type_info,
                               &(Asset_ID) { .name = name });
  if (asset) {
    return asset->id;
  }
  return (EID)-1;
}

// 0 is returned on success
INTERNAL int
AddAsset(Asset_Manager* am, EID entity, const char* name,
         const Sparse_Set* storage, Asset_Reload_Func reload_func, void* udata)
{
  Asset_ID* asset = FHT_Insert(&am->asset_ids, &am->asset_id_type_info,
                               &(Asset_ID) { .name = name });
  if (asset) {
    asset->id = entity;
    asset->set = storage;
    asset->reload_func = reload_func;
    asset->udata = udata;
    return 0;
  }
  return -1;
}

INTERNAL void
UpdateAssets(Asset_Manager* am)
{
  const char* changed_files[256];
  size_t num_changed = PlatformDataDirectoryModified(changed_files, ARR_SIZE(changed_files));
  for (size_t i = 0; i < num_changed; i++) {
    // TODO: detect if voxel models, fonts or bitmaps are changed and reload them
    Asset_ID* asset = FHT_Search(&am->asset_ids, &am->asset_id_type_info,
                                 &(Asset_ID) { .name = changed_files[i] });
    if (asset && asset->reload_func) {
      // reload asset
      asset->reload_func(SearchSparseSet(asset->set, asset->id),
                         asset->name,
                         asset->udata);
      LOG_TRACE("reloaded asset '%s'", asset->name);
    }
  }
}

INTERNAL Voxel_Grid*
AddVoxelGridComponent(ECS* ecs, Asset_Manager* am, Allocator* allocator,
                      EID entity, const char* name)
{
  Voxel_Grid* vox = AddComponent(ecs, Voxel_Grid, entity);
  LoadVoxelGridFromFile(allocator, vox, name);
  AddAsset(am, entity, name, &g_sparse_set_Voxel_Grid,
           VoxelGrid_ReloadFunc, allocator);
  return vox;
}

INTERNAL Pipeline_Program*
AddPipelineProgramComponent(ECS* ecs, Asset_Manager* am, EID entity,
                            const char* vertex_shader, const char* fragment_shader,
                            Pipeline_Create_Func create_func, Deletion_Queue* dq)
{
  Pipeline_Program* prog = AddComponent(ecs, Pipeline_Program, entity);
  prog->create_func = create_func;
  prog->vertex_shader = vertex_shader;
  prog->fragment_shader = fragment_shader;
  // we don't load any shaders or compile them, pipeline creation is deferred and batched
  AddAsset(am, entity, vertex_shader, &g_sparse_set_Pipeline_Program,
           PipelineProgram_ReloadFunc, dq);
  // some pipelines might have no pixel shader
  if (fragment_shader) {
    AddAsset(am, entity, fragment_shader, &g_sparse_set_Pipeline_Program,
             PipelineProgram_ReloadFunc, dq);
  }
  return prog;
}

INTERNAL VkResult
BatchCreatePipelines()
{
  uint32_t count = ComponentCount(Pipeline_Program);
  Pipeline_Program* progs = ComponentData(Pipeline_Program);
  Pipeline_Desc* descs = PersistentAllocate(count * sizeof(Pipeline_Desc));
  VkPipeline* pipelines = PersistentAllocate(count * sizeof(VkPipeline));
  VkPipelineLayout* layouts = PersistentAllocate(count * sizeof(VkPipelineLayout));
  for (uint32_t i = 0; i < count; i++) {
    progs[i].create_func(&descs[i]);
    descs[i].vertex_shader = progs[i].vertex_shader;
    descs[i].fragment_shader = progs[i].fragment_shader;
  }
  uint32_t start = PlatformGetTicks();
  VkResult err = CreateGraphicsPipelines(pipelines, count, descs, layouts);
  uint32_t end = PlatformGetTicks();
  LOG_INFO("created graphics pipelines in %u ms", end - start);
  for (uint32_t i = 0; i < count; i++) {
    progs[i].pipeline = pipelines[i];
    progs[i].layout = layouts[i];
  }
  PersistentRelease(layouts);
  PersistentRelease(pipelines);
  PersistentRelease(descs);
  if (err != VK_SUCCESS) {
    LOG_ERROR("failed to batch create pipelines with error %s", ToString_VkResult(err));
    return err;
  }
  return VK_SUCCESS;
}

// INTERNAL Font*
// AddFontComponent(ECS* ecs, Asset_Manager* am, Bitmap_Renderer* br, Font_Atlas* fa, VkCommandBuffer cmd,
//                  EID entity, const char* path, uint32_t pixel_size)
// {
//   Font* font = AddComponent(ecs, entity, &type_info_Font);
//   LoadToFontAtlas(br, fa, cmd, entity, path, pixel_size);
//   font->udata = fa;
//   AddAsset(am, entity, path, &type_info_Font, &Font_ReloadFunc, br);
// }
