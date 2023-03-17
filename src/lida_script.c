/*

  lida engine native scripting system.

  NOTE: personally I like more embedding some virtual machine
  language, but it would take a lot of time. Maybe we will implement
  some kind of Lisp or just some other scripting language like Python.

 */

typedef struct Script Script;

// TODO: make dt a global variable
typedef void(*Script_Func)(Script* script, EID entity, float dt);

typedef union {

  int64_t  int_64;
  int32_t int_32;
  uint64_t uint_64;
  uint32_t uint_32;
  float float_32;

} Script_Arg;

struct Script {

  const char* name;
  Script_Func func;
  void* udata;
  Script_Arg arg0;
  Script_Arg arg1;
  Script_Arg arg2;
  Script_Arg arg3;
  // FIXME: do we really need this? Time will show.
  int frequency;

};
DECLARE_COMPONENT(Script);

typedef struct {

  const char* name;
  Script_Func func;

} Script_Entry;
DECLARE_TYPE(Script_Entry);

typedef struct {

  Fixed_Hash_Table scripts;

} Script_Manager;

Script_Manager* g_script_manager;


/// private functions

INTERNAL uint32_t
HashScriptEntry(const void* obj)
{
  const Script_Entry* script = obj;
  return HashString32(script->name);
}

INTERNAL int
CompareScriptEntries(const void* lhs, const void* rhs)
{
  const Script_Entry* l = lhs, *r = rhs;
  return strcmp(l->name, r->name);
}

INTERNAL void
RegisterScript(Script_Manager* sm, const char* name, Script_Func func)
{
  Script_Entry entry = { .name = name, .func = func };
  FHT_Insert(&sm->scripts, GET_TYPE_INFO(Script_Entry), &entry);
}

INTERNAL Script_Func
GetScript(Script_Manager* sm, const char* name)
{
  Script_Entry entry = { .name = name };
  Script_Entry* it = FHT_Search(&sm->scripts, GET_TYPE_INFO(Script_Entry), &entry);
  if (it)
    return it->func;
  return NULL;
}


/// scripts

#define DEFSCRIPT(name) INTERNAL void SCRIPT_##name (Script* self, EID entity, float dt)

DEFSCRIPT(rotate_voxel)
{
  Transform* transform = GetComponent(Transform, entity);
  Quat rot;
  float vx = self->arg0.float_32;
  float vy = self->arg1.float_32;
  float vz = self->arg2.float_32;
  QuatFromEulerAngles(dt * vx, dt * vy, dt * vz, &rot);
  MultiplyQuats(&transform->rotation, &rot, &transform->rotation);
}

// changes a random position each 100 frames
DEFSCRIPT(change_voxel)
{
  (void)self;
  (void)dt;
  Voxel_Grid* grid = GetComponent(Voxel_Grid, entity);
  uint32_t x = (grid->hash ^ 0xf9432aa84beb) % grid->width;
  uint32_t y = (grid->hash ^ 0x48db57c487a3) % grid->width;
  uint32_t z = (grid->hash ^ 0x98aff843be81) % grid->width;
  Voxel vox = grid->hash % 256;
  SetInVoxelGrid(grid, x, y, z, vox);
}


/// public functions

INTERNAL void
InitScripts(Script_Manager* sm)
{
  const uint32_t size = 16;
  REGISTER_TYPE(Script_Entry, &HashScriptEntry, &CompareScriptEntries);
  FHT_Init(&sm->scripts,
           PersistentAllocate(FHT_CALC_SIZE(GET_TYPE_INFO(Script_Entry), size)),
           size, GET_TYPE_INFO(Script_Entry));
  // insert all scripts to hash table
#define REGISTER_SCRIPT(sm, name) RegisterScript(sm, #name, SCRIPT_##name)
  REGISTER_SCRIPT(sm, rotate_voxel);
  REGISTER_SCRIPT(sm, change_voxel);
}
