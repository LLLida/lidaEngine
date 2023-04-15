/*
  Builtin console.

  TODO: implement scrollback.

 */

typedef struct {

  char* ptr;
  uint32_t color;

} Console_Line;

typedef void(*Console_Command_Func)(uint32_t num, const char** args);

typedef struct {

  const char* name;
  Console_Command_Func func;
  const char* doc;

} Console_Command;
DECLARE_TYPE(Console_Command);

typedef struct {

  char buff[512];
  char* completions[32];
  uint32_t buff_offset;

} Console_Completion_Context;

typedef struct {

  Keymap keymap;
  float bottom;
  float target_y;
  // recommended value: 6.0
  float open_speed;
  uint32_t bg_color1;
  uint32_t bg_color2;
  uint32_t fg_color1;
  uint32_t fg_color2;
  uint32_t cursor_color1;
  EID font;
  int cursor_pos;
  uint32_t last_line;
  uint32_t num_lines;
  uint32_t buff_offset;
  Fixed_Hash_Table env;
  char prompt[256];
  Console_Line lines[128];
  char buffer[8*1024];
  char history_buffer[512];
  uint32_t hst_offset;
  uint32_t last_hst;
  char* hst_lines[16];
  uint32_t num_hst_lines;

} Console;

GLOBAL Console* g_console;


/// private functions

INTERNAL void
ShowConsole()
{
  g_console->target_y = 0.3f;
  BindKeymap(&g_console->keymap);
}

INTERNAL void
ShowConsoleBig()
{
  ShowConsole();
  g_console->target_y = 0.75f;
}

INTERNAL void
HideConsole()
{
  g_console->target_y = 0.0f;
  UnbindKeymap();
}

// pass color=0 to use default color
// NOTE: str is copied, no worries
INTERNAL void
ConsolePutLine(const char* str, uint32_t color)
{
  size_t len = strlen(str);
  if (g_console->buff_offset + len >= sizeof(g_console->buffer)) {
    g_console->buff_offset = 0;
    // Fixed a horrible bug... If this log is put before buff_offset = 0
    // then we would have infinite recursion calls to ConsolePutLine.
    LOG_WARN("console buffer is out of space, rewriting from begin...");
  }
  g_console->last_line = (g_console->last_line+1) % ARR_SIZE(g_console->lines);
  if (color == 0) {
    color = g_console->fg_color2;
  }
  g_console->lines[g_console->last_line] = (Console_Line) {
    .ptr = strcpy(g_console->buffer+g_console->buff_offset, str),
    .color = color
  };
  g_console->buff_offset += len+1;
  if (g_console->num_lines < ARR_SIZE(g_console->lines))
    g_console->num_lines++;
}

INTERNAL void
UpdateConsoleState(float dt)
{
  g_console->bg_color1 = PACK_COLOR(35, 0, 0, 200);
  g_console->bg_color2 = PACK_COLOR(40, 10, 8, 240);
  g_console->fg_color1 = PACK_COLOR(255, 222, 173, 255);
  g_console->fg_color2 = PACK_COLOR(222, 184, 135, 255);

  g_console->cursor_color1 = PACK_COLOR(50, 205, 50, 245);

  // TODO: currently we exponential grow. It does look nice when it
  // starts opening, but when it is almost done. It decreases very
  // slow.  Maybe we could introduce something better. (please don't
  // use lerp, it looks ugly)
  // NOTE: I found solution: just increase open_speed😎
  float dir = g_console->target_y - g_console->bottom;
  // HACK: this doesn't make console flicker when it's close to target_y
  if (fabs(dir) < 0.001f) g_console->bottom = g_console->target_y;
  g_console->bottom += dir * dt * g_console->open_speed;
}

INTERNAL void
DrawConsole(Quad_Renderer* renderer)
{
  if (g_console->bottom < 0.001f)
    return;
  Font* font = GetComponent(Font, g_console->font);
  const float prompt_height = 0.04f;
  Vec2 char_size;
  {
    int* option = GetVar_Int(g_config, "Console.pixel_perfect_font_size");
    if (option && *option) {
      PixelPerfectCharSize(font->pixel_size, &char_size);
    } else {
      char_size.x = 0.025f;
      char_size.y = 0.025f;
    }
  }
  // draw quads
  Vec2 pos = {0.0f, g_console->bottom - prompt_height};
  Vec2 size = {1.0f, prompt_height};
  DrawQuad(renderer, &pos, &size, g_console->bg_color1, 0);
  pos.y = 0.0f;
  size.y = g_console->bottom - prompt_height;
  DrawQuad(renderer, &pos, &size, g_console->bg_color2, 1);
  // draw lines
  const float left_pad = 0.01f;
  const float bottom_pad = 0.01f;
  pos.x = left_pad;
  pos.y = g_console->bottom - prompt_height - bottom_pad;
  size.x = char_size.x;
  size.y = char_size.y;
  uint32_t count = g_console->num_lines;
  while (count > 0 && pos.y >= 0.0f) {
    uint32_t id = g_console->last_line + ARR_SIZE(g_console->lines) - g_console->num_lines + count;
    Console_Line* line = &g_console->lines[id % ARR_SIZE(g_console->lines)];
    // TODO(consistency): change argument orger
    DrawText(renderer, font, line->ptr, &size, line->color, &pos);
    pos.y -= char_size.y;
    count--;
  }
  // draw cursor
  // Why human text is so complex??? 😵
  char current_char = g_console->prompt[g_console->cursor_pos];
  pos.x = left_pad + font->glyphs[(int)current_char].bearing.x * char_size.x;
  for (int i = 0; i < g_console->cursor_pos; i++) {
    pos.x += font->glyphs[(int)g_console->prompt[i]].advance.x * char_size.x;
  }
  pos.y = g_console->bottom - char_size.y - 0.5f * bottom_pad;
  // Space has size.x = 0, which does not satisfy us
  if (current_char == ' ' || current_char == '\0') {
    // 'X' is big. it definitely fits well
    size.x = font->glyphs['X'].size.x * char_size.x;
  } else {
    size.x = font->glyphs[(int)current_char].size.x * char_size.x;
  }
  size.y = char_size.y;
  DrawQuad(renderer, &pos, &size, g_console->cursor_color1, 0);
  // draw prompt text
  pos.x = left_pad;
  pos.y = g_console->bottom - bottom_pad;
  size.x = char_size.x;
  size.y = char_size.y;
  DrawText(renderer, font, g_console->prompt, &size, g_console->fg_color1, &pos);
}

INTERNAL void
ConsoleAddCompletionCandidate(const Traverse_String_Info* info)
{
  Console_Completion_Context* context = info->udata;
  size_t len = strlen(info->buff);
  if (info->id > ARR_SIZE(context->completions) ||
      context->buff_offset + len > sizeof(context->buff)) {
    // out of memory
    LOG_WARN("too many completions");
    return;
  }
  strcpy(&context->buff[context->buff_offset], info->buff);
  context->completions[info->id] = &context->buff[context->buff_offset];
  context->buff_offset += len+1;
}

INTERNAL int
ConsoleKeymap_Pressed(PlatformKeyCode key, void* udata)
{
  (void)udata;
  size_t prompt_len = strlen(g_console->prompt);
  switch (key)
    {

    case PlatformKey_ESCAPE:
      HideConsole();
      break;

      // '`' toggles between console sizes
    case PlatformKey_BACKQUOTE:
      HideConsole();
      if (g_console->bottom > 0.5f) {
        ShowConsole();
      } else {
        ShowConsoleBig();
      }
      break;

    case PlatformKey_LEFT:
      if (g_console->cursor_pos > 0)
        g_console->cursor_pos -= 1;
      break;

    case PlatformKey_RIGHT:
      if (g_console->cursor_pos < (int)prompt_len)
        g_console->cursor_pos += 1;
      break;

      // delete character
    case PlatformKey_BACKSPACE:
      if (g_console->cursor_pos > 0) {
        char* dst = g_console->prompt + g_console->cursor_pos - 1;
        memmove(dst, dst+1, prompt_len-1);
        g_console->prompt[prompt_len-1] = '\0';
        g_console->cursor_pos--;
      }
      break;

    case PlatformKey_RETURN:
      {
        // put prompt to history
        if (strlen(g_console->prompt) > 0) {
          size_t len = strlen(g_console->prompt);
          if (g_console->hst_offset + len >= sizeof(g_console->history_buffer)) {
            g_console->hst_offset = 0;
          }
          g_console->last_hst = (g_console->last_hst + 1) % ARR_SIZE(g_console->hst_lines);
          g_console->hst_lines[g_console->last_hst] = strcpy(g_console->history_buffer+g_console->hst_offset, g_console->prompt);
          g_console->hst_offset += len+1;
          if (g_console->num_hst_lines < ARR_SIZE(g_console->hst_lines))
            g_console->num_hst_lines++;
        }
        // collect arguments
        // NOTE: overflow can't happen because prompt's max size is 256
        char buff[512];
        char* words[16];
        uint32_t offset = 0;
        uint32_t num_words = 0;
        char* word = strtok(g_console->prompt, " ");
        while (word != NULL) {
          if (num_words == ARR_SIZE(words)) {
            LOG_WARN("maximum number of arguments is exceeded(which is %lu)", ARR_SIZE(words)-1);
            break;
          }
          size_t len = strlen(word);
          strcpy(buff+offset, word);
          words[num_words++] = buff+offset;
          offset += len+1;
          word = strtok(NULL, " ");
        }

        if (num_words == 0) {
          ConsolePutLine("", 0);
        }
        // search command
        Console_Command* command = FHT_Search(&g_console->env,
                                              GET_TYPE_INFO(Console_Command), &words[0]);
        if (command == NULL) {
          LOG_WARN("command '%s' does not exist", words[0]);
        } else {
          command->func(num_words-1, (const char**)words+1);
        }
        // ConsolePutLine(g_console->prompt, 0);
        // clear prompt
        g_console->prompt[0] = '\0';
        g_console->cursor_pos = 0;
      } break;

    case PlatformKey_TAB:
      {
        // tokenize prompt
        char buff[512];
        char* words[16];
        uint32_t offset = 0;
        uint32_t num_words = 0;
        // TODO(bug): search until g_console->cursor_pos
        char* prompt = strcpy(buff+255, g_console->prompt);
        char* word = strtok(prompt, " ");
        while (word != NULL) {
          if (num_words == ARR_SIZE(words)) {
            LOG_WARN("maximum number of arguments is exceeded(which is %lu)", ARR_SIZE(words)-1);
            break;
          }
          size_t len = strlen(word);
          strcpy(buff+offset, word);
          words[num_words++] = buff+offset;
          offset += len+1;
          word = strtok(NULL, " ");
        }
        if (num_words == 0)
          break;
        // perform autcompletion
        Console_Completion_Context context = { .buff_offset = 0 };
        if (num_words == 1) {
          // TODO: complete command name.
          // This would need us to store commands in search trees also.
        } else {
          // complete variable name
          size_t num_completions = ListVarsPrefix(g_config, &ConsoleAddCompletionCandidate,
                                                  words[num_words-1], &context);
          if (num_completions == 0) {
            ConsolePutLine("No completions", PACK_COLOR(30, 30, 30, 150));
          } else if (num_completions == 1) {
            uint32_t base = g_console->cursor_pos;
            while (g_console->prompt[base] != ' ') {
              base--;
            }
            uint32_t offset = g_console->cursor_pos - 1 - base;
            strcpy(g_console->prompt + g_console->cursor_pos,
                   context.completions[0] + offset);
            g_console->cursor_pos += strlen(context.completions[0]) - offset;
          } else {
            // list completions
            ConsolePutLine("---", PACK_COLOR(30, 30, 30, 150));
            for (size_t i = 0; i < num_completions; i++) {
              ConsolePutLine(context.completions[i], 0);
            }
            // TODO(convenience): complete common part like Emacs does
          }
        }
      } break;

    case PlatformKey_UP:
      strcpy(g_console->prompt, g_console->hst_lines[g_console->last_hst]);
      g_console->cursor_pos = 0;
      if (g_console->last_hst == 0) {
        if (g_console->num_hst_lines != ARR_SIZE(g_console->hst_lines)) {
          g_console->last_hst = ARR_SIZE(g_console->hst_lines)-1;
        }
      } else {
        g_console->last_hst--;
      }
      break;

    default:
      break;

    }
  return 0;
}

INTERNAL int
ConsoleKeymap_Mouse(int x, int y, float xrel, float yrel, void* udata)
{
  (void)x;
  (void)y;
  (void)xrel;
  (void)yrel;
  (void)udata;
  return 0;
}

INTERNAL void
ConsoleKeymap_TextInput(const char* text)
{
  if (text[0] == '`' || text[0] == '~')
    return;
  size_t prompt_len = strlen(g_console->prompt);
  if (prompt_len+1 == sizeof(g_console->prompt))
    return;
  char* src = g_console->prompt + g_console->cursor_pos;
  memmove(src+1, src, prompt_len - g_console->cursor_pos + 1);
  g_console->prompt[g_console->cursor_pos] = text[0];
  g_console->cursor_pos++;
}

INTERNAL void
ConsoleLogCallback(const Log_Event* le)
{
  uint32_t colors[6] = {
    PACK_COLOR(69, 69, 69, 200),   // TRACE
    PACK_COLOR(154, 205, 50, 240), // DEBUG
    PACK_COLOR(46, 139, 87, 250),  // INFO
    PACK_COLOR(253, 165, 10, 255), // WARN
    PACK_COLOR(205, 3, 2, 255),    // ERROR
    PACK_COLOR(138, 43, 210, 253), // FATAL
  };
  ConsolePutLine(le->str, colors[le->level]);
}

INTERNAL void
ConsoleAddCommand(const char* name, Console_Command_Func func, const char* doc)
{
  Console_Command command = { name, func, doc };
  FHT_Insert(&g_console->env, GET_TYPE_INFO(Console_Command), &command);
}

INTERNAL uint32_t
HashConsoleCommand(const void* obj)
{
  const Console_Command* command = obj;
  return HashString32(command->name);
}

INTERNAL int
CompareConsoleCommands(const void* lhs, const void* rhs)
{
  const Console_Command* l = lhs, *r = rhs;
  return strcmp(l->name, r->name);
}

/// list of commands
INTERNAL void CMD_info(uint32_t num, const char** args);
INTERNAL void CMD_FPS(uint32_t num, const char** args);
INTERNAL void CMD_get(uint32_t num, const char** args);
INTERNAL void CMD_set(uint32_t num, const char** args);
INTERNAL void CMD_list_vars(uint32_t num, const char** args);
INTERNAL void CMD_clear_scene(uint32_t num, const char** args);
INTERNAL void CMD_load_voxel(uint32_t num, const char** args);
INTERNAL void CMD_save_scene(uint32_t num, const char** args);
INTERNAL void CMD_load_scene(uint32_t num, const char** args);
INTERNAL void CMD_make_voxel_rotate(uint32_t num, const char** args);
INTERNAL void CMD_list_entities(uint32_t num, const char** args);
INTERNAL void CMD_make_voxel_change(uint32_t num, const char** args);
INTERNAL void CMD_spawn_sphere(uint32_t num, const char** args);
INTERNAL void CMD_spawn_cube(uint32_t num, const char** args);
INTERNAL void CMD_remove_script(uint32_t num, const char** args);
INTERNAL void CMD_set_voxel_backend(uint32_t num, const char** args);
INTERNAL void CMD_spawn_random_voxels(uint32_t num, const char** args);
INTERNAL void CMD_print_transform(uint32_t num, const char** args);
INTERNAL void CMD_remove_voxel(uint32_t num, const char** args);
INTERNAL void CMD_spawn_random_vox_models(uint32_t num, const char** args);
INTERNAL void CMD_voxel_buff_statistics(uint32_t num, const char** args);
INTERNAL void CMD_spawn_melon_floor(uint32_t num, const char** args);


/// public functions

INTERNAL void
InitConsole()
{
  g_console = PersistentAllocate(sizeof(Console));
  g_console->open_speed = 6.0f;
  g_console->bottom = 0.0f;
  g_console->target_y = 0.0f;
  g_console->last_line = ARR_SIZE(g_console->lines)-1;
  g_console->num_lines = 0;
  g_console->buff_offset = 0;
  g_console->cursor_pos = 0;
  g_console->hst_offset = 0;
  g_console->last_hst = ARR_SIZE(g_console->hst_lines)-1;
  g_console->num_hst_lines = 0;
  g_console->keymap = (Keymap) { ConsoleKeymap_Pressed,
                                 NULL,
                                 ConsoleKeymap_Mouse,
                                 ConsoleKeymap_TextInput,
                                 NULL };
  g_console->prompt[0] = '\0';
  ConsolePutLine("lida engine console. Use command 'info' for help.", 0);
  EngineAddLogger(&ConsoleLogCallback, 0, NULL);
  REGISTER_TYPE(Console_Command, HashConsoleCommand, CompareConsoleCommands);
  const uint32_t max_commands = 64;
  FHT_Init(&g_console->env,
           PersistentAllocate(FHT_CALC_SIZE(GET_TYPE_INFO(Console_Command), max_commands)),
           max_commands, GET_TYPE_INFO(Console_Command));
#define ADD_COMMAND(cmd, doc) ConsoleAddCommand(#cmd, CMD_##cmd, doc)
  ADD_COMMAND(info,
              "info COMMAND-NAME\n"
              " Print information about command.");
  ADD_COMMAND(FPS,
              "FPS\n"
              " Print number of frames per second we're running at.");
  ADD_COMMAND(get,
              "get VARIABLE-NAME\n"
              " Print value of configuration variable.");
  ADD_COMMAND(set,
              "set VARIABLE-NAME [INTEGER FLOAT STRING]\n"
              " Set value of configuration variable.");
  ADD_COMMAND(list_vars,
              "list_vars [PREFIX]\n"
              " List all configuration variables beginning with prefix.\n"
              " If prefix not specified than list all variables.");
  ADD_COMMAND(clear_scene,
              "clear_scene\n"
              " Destroy all voxel models.");
  ADD_COMMAND(load_voxel,
              "load_voxel FILE X Y Z [S]\n"
              " Load voxel model from FILE and translate to position [X Y Z].\n"
              " S is scale. Default value is 1.0.");
  ADD_COMMAND(save_scene,
              "save_scene FILE\n"
              " Save this scene to FILE.");
  ADD_COMMAND(load_scene,
              "load_scene FILE\n"
              " Load scene from FILE.");
  ADD_COMMAND(make_voxel_rotate,
              "make_voxel_rotate ENTITY X Y Z\n"
              " Make ENTITY rotate.\n"
              " X - yaw.\n"
              " Y - pitch.\n"
              " Z - roll.");
  ADD_COMMAND(list_entities,
              "list_entities\n"
              " List all entities in this scene.");
  ADD_COMMAND(make_voxel_change,
              "make_voxel_change ENTITY [FREQUENCY]\n"
              " Make a random voxel change in ENTITY's grid.\n"
              " Each FREQUENCY frames a random voxel will be changed.");
  ADD_COMMAND(spawn_sphere,
              "spawn_sphere RADIUS [R G B] [X Y Z] [S]\n"
              " Spawn sphere built from voxels with RADIUS.\n"
              " R, G, B - color components of sphere.\n"
              " X, Y, Z - position in global space.\n"
              " S - scale.");
  ADD_COMMAND(remove_script,
              "remove_script ENTITY\n"
              " Deattach script from ENTITY.");
  ADD_COMMAND(spawn_cube,
              "spawn_cube [W H D] [R G B] [X Y Z] [S]\n"
              " Spawn cube.\n"
              " W, H, D - cube extents.\n"
              " R, G, B - color components of cube.\n"
              " X, Y, Z - position in global space.\n"
              " S - scale.");
  ADD_COMMAND(set_voxel_backend,
              "set_voxel_backend BACKEND\n"
              " Set voxel rendering backend.\n"
              " BACKEND can either be 'indirect' or 'classic'.");
  ADD_COMMAND(spawn_random_voxels,
              "spawn_random_voxels NUMBER\n"
              " Spawn NUMBER voxel models rotated and translated randomly.\n"
              " They can be either spheres, cubes or some fractals.");
  ADD_COMMAND(print_transform,
              "print_transform ENTITY\n"
              " Print entity's transform component.");
  ADD_COMMAND(remove_voxel,
              "remove_voxel ENITY\n"
              " Remove voxel model from scene.");
  ADD_COMMAND(spawn_random_vox_models,
              "spawn_random_vox_models NUMBER [DISTRIB]\n"
              " Spawn NUMBER random voxel models specified in CVar Misc.vox_models.\n"
              " DISTRIB is width of distribution.");
  ADD_COMMAND(voxel_buff_statistics,
              "voxel_buff_statistics\n"
              " Print memory statistics of voxels.");
  ADD_COMMAND(spawn_melon_floor,
              "spawn_melon_floor [TYPE]\n"
              " Spawn a floor with melon colors.\n"
              " TYPE is either 'melon'(default) or 'chess'.");
}

INTERNAL void
FreeConsole()
{
  PersistentRelease(g_console->env.ptr);
  PersistentRelease(g_console);
}

INTERNAL void
UpdateAndDrawConsole(Quad_Renderer* renderer, float dt)
{
  UpdateConsoleState(dt);
  DrawConsole(renderer);
}

#define CMD_ARG_COUNT_MISMATCH(argnum) \
  LOG_WARN("this command accepts %s arguments; see 'info %s'", argnum, __func__ + 3); \
  return

void
CMD_info(uint32_t num, const char** args)
{
  if (num > 1) {
    CMD_ARG_COUNT_MISMATCH("0 or 1");
  }
  if (num == 0) {
    // list all commands
    ConsolePutLine("Listing all commands:", 0);
    FHT_Iterator it;
    FHT_FOREACH(&g_console->env, GET_TYPE_INFO(Console_Command), &it) {
      Console_Command* command = FHT_IteratorGet(&it);
      // HACK: recursively call CMD_info
      CMD_info(1, &command->name);
      ConsolePutLine("---", 0);
    }
    return;
  }
  char* begin = (char*)args[0];
  Console_Command* command = FHT_Search(&g_console->env, GET_TYPE_INFO(Console_Command), &begin);
  if (command == 0) {
    LOG_WARN("command '%s' does not exist", begin);
    return;
  }
  char buff[512];
  strncpy(buff, command->doc, sizeof(buff)-1);
  begin = buff;
  char* it = begin;
  while (*it) {
    if (*it == '\n') {
      *it = '\0';
      ConsolePutLine(begin, PACK_COLOR(152, 252, 152, 233));
      begin = it+1;
    }
    it++;
  }
  ConsolePutLine(begin, PACK_COLOR(152, 252, 152, 233));
}

void
CMD_FPS(uint32_t num, const char** args)
{
  (void)args;
  if (num != 0) {
    CMD_ARG_COUNT_MISMATCH("no");
  }
  LOG_INFO("FPS=%f", g_window->frames_per_second);
}

void
CMD_get(uint32_t num, const char** args)
{
  if (num != 1) {
    CMD_ARG_COUNT_MISMATCH("only 1");
  }
  CVar* var = TST_Search(g_config, g_config->root, args[0]);
  if (var == NULL) {
    LOG_WARN("variable '%s' does not exist", args[0]);
    return;
  }
  char buff[16];
  switch (var->type)
    {
    case CONFIG_INTEGER:
      stbsp_sprintf(buff, "%d", var->value.int_);
      ConsolePutLine(buff, 0);
      break;

    case CONFIG_FLOAT:
      stbsp_sprintf(buff, "%f", var->value.float_);
      ConsolePutLine(buff, 0);
      break;

    case CONFIG_STRING:
      ConsolePutLine(var->value.str, 0);
      break;
    }
}

void
CMD_set(uint32_t num, const char** args)
{
  if (num != 2) {
    CMD_ARG_COUNT_MISMATCH("only 2");
  }
  CVar* var = TST_Search(g_config, g_config->root, args[0]);
  if (var == NULL) {
    LOG_WARN("variable '%s' does not exist", args[0]);
    return;
  }
  char buff[16];
  const char* val = args[1];
  if ((val[0] >= '0' && val[0] <= '9') || val[0] == '-') {
    // TODO(quality): check for parse errors
    if (strchr(val+1, '.')) {
      if (var->type != CONFIG_FLOAT) {
        LOG_WARN("set: '%s' is not a float", args[0]);
        return;
      }
      var->value.float_ = strtof(val, NULL);
      stbsp_sprintf(buff, "%f", var->value.float_);
      ConsolePutLine(buff, 0);
    } else {
      if (var->type != CONFIG_INTEGER) {
        LOG_WARN("set: '%s' is not a integer", args[0]);
        return;
      }
      var->value.int_ = atoi(val);
      stbsp_sprintf(buff, "%d", var->value.int_);
      ConsolePutLine(buff, 0);
    }
  } else {
    if (var->type != CONFIG_STRING) {
      LOG_WARN("set: '%s' is not a string", args[0]);
      return;
    }
    var->value.str = g_config->buff + g_config->buff_offset;
    g_config->buff_offset += strlen(val) + 1;
    strcpy(var->value.str, val);
    ConsolePutLine(val, 0);
  }
}

INTERNAL void
list_vars_Traverse_Func(const Traverse_String_Info* str)
{
  // TODO(convenience): print value of variable
  ConsolePutLine(str->buff, 0);
}

void
CMD_list_vars(uint32_t num, const char** args)
{
  if (num > 1) {
    CMD_ARG_COUNT_MISMATCH("0 or 1");
  }
  if (num == 0) {
    ListVars(g_config, &list_vars_Traverse_Func, NULL);
  } else {
    ListVarsPrefix(g_config, &list_vars_Traverse_Func, args[0], NULL);
  }
}

void
CMD_clear_scene(uint32_t num, const char** args)
{
  (void)args;
  if (num != 0) {
    CMD_ARG_COUNT_MISMATCH("no");
  }
  FOREACH_COMPONENT(Voxel_Grid) {
    FreeVoxelGrid(g_vox_allocator, &components[i]);
  }
  UNREGISTER_COMPONENT(g_ecs, Voxel_Grid);
  UNREGISTER_COMPONENT(g_ecs, Script);
  UNREGISTER_COMPONENT(g_ecs, Transform);
  UNREGISTER_COMPONENT(g_ecs, OBB);
  UNREGISTER_COMPONENT(g_ecs, Voxel_View);
  DestroyEmptyEntities(g_ecs);
  ClearVoxelDrawerCache(g_vox_drawer);
}

void
CMD_load_voxel(uint32_t num, const char** args)
{
  if (num != 5 && num != 4) {
    CMD_ARG_COUNT_MISMATCH("4 or 5");
  }
  EID entity = CreateEntity(g_ecs);
  if (LoadVoxModel(g_ecs, g_asset_manager, g_vox_allocator,
                   entity, args[0]) == NULL) {
    return;
  }
  Transform* transform = AddComponent(g_ecs, Transform, entity);
  transform->rotation = QUAT_IDENTITY();
  // TODO(convenience): check for parse errors
  transform->position.x = strtof(args[1], NULL);
  transform->position.y = strtof(args[2], NULL);
  transform->position.z = strtof(args[3], NULL);
  if (num == 5) {
    transform->scale = strtof(args[4], NULL);
  } else {
    transform->scale = 1.0f;
  }
  AddComponent(g_ecs, OBB, entity);
}

void
CMD_make_voxel_rotate(uint32_t num, const char** args)
{
  if (num != 4) {
    CMD_ARG_COUNT_MISMATCH("4");
  }
  EID entity = atoi(args[0]);
  Voxel_View* view = GetComponent(Voxel_View, entity);
  if (view == NULL) {
    LOG_WARN("entity %u is not an voxel model", entity);
    return;
  }
  Script* script = AddComponent(g_ecs, Script, entity);
  if (script == NULL) {
    script = GetComponent(Script, entity);
    LOG_WARN("entity %u already has script component '%s'", entity, script->name);
    return;
  }
  script->name = "rotate_voxel";
  script->func = GetScript(g_script_manager, "rotate_voxel");
  script->arg0.float_32 = strtof(args[1], NULL);
  script->arg1.float_32 = strtof(args[2], NULL);
  script->arg2.float_32 = strtof(args[3], NULL);
  script->frequency = 1;
}

void
CMD_list_entities(uint32_t num, const char** args)
{
  (void)args;
  if (num > 0) {
    CMD_ARG_COUNT_MISMATCH("no");
  }
  uint32_t* entities = g_ecs->entities->ptr;
  for (EID eid = 0; eid < g_ecs->max_entities; eid++) {
    if ((entities[eid] & ENTITY_DEAD_MASK) == 0) {
      LOG_INFO("entity %u has %u components", eid, entities[eid]);
    }
  }
}

void
CMD_make_voxel_change(uint32_t num, const char** args)
{
  if (num > 2) {
    CMD_ARG_COUNT_MISMATCH("only 1");
  }
  EID entity = atoi(args[0]);
  Script* script = AddComponent(g_ecs, Script, entity);
  if (script == NULL) {
    script = GetComponent(Script, entity);
    LOG_WARN("entity %u already has script component '%s'", entity, script->name);
    return;
  }
  script->name = "change_voxel";
  script->func = GetScript(g_script_manager, "change_voxel");
  if (num == 2) {
    script->frequency = atoi(args[1]);
  } else {
    script->frequency = 100;
  }
}

void
CMD_spawn_sphere(uint32_t num, const char** args)
{
  if (num != 1 && num != 4 && num != 7 && num != 8) {
    CMD_ARG_COUNT_MISMATCH("1, 4, 7, or 8");
  }
  EID entity = CreateEntity(g_ecs);
  Voxel_View* view = AddComponent(g_ecs, Voxel_View, entity);
  view->grid = CreateEntity(g_ecs);
  Voxel_Grid* grid = AddComponent(g_ecs, Voxel_Grid, view->grid);
  int radius = atoi(args[0]);
  AllocateVoxelGrid(g_vox_allocator, grid, radius*2+1, radius*2+1, radius*2+1);
  if (num == 1) {
    grid->palette[1] = PACK_COLOR(240, 240, 240, 255);
  } else {
    int r = atoi(args[1]);
    int g = atoi(args[2]);
    int b = atoi(args[3]);
    grid->palette[1] = PACK_COLOR(r, g, b, 255);
  }
  GenerateVoxelSphere(grid, radius, 1);
  // don't forget to update hash
  RehashVoxelGrid(grid);
  Transform* transform = AddComponent(g_ecs, Transform, entity);
  transform->rotation = QUAT_IDENTITY();
  if (num < 4) {
    transform->position.x = 0.0f;
    transform->position.y = 0.0f;
    transform->position.z = 0.0f;
    transform->scale = 1.0f;
  } else {
    transform->position.x = strtof(args[4], NULL);
    transform->position.y = strtof(args[5], NULL);
    transform->position.z = strtof(args[6], NULL);
    if (num == 8) {
      transform->scale = strtof(args[7], NULL);
    } else {
      transform->scale = 1.0f;
    }
  }
  AddComponent(g_ecs, OBB, entity);
}

void
CMD_spawn_cube(uint32_t num, const char** args)
{
  if (num != 3 && num != 6 && num != 9 && num != 10) {
    CMD_ARG_COUNT_MISMATCH("3, 5, 9, or 10");
  }
  EID entity = CreateEntity(g_ecs);
  uint32_t width = atoi(args[0]);
  uint32_t height = atoi(args[1]);
  uint32_t depth = atoi(args[2]);
  Voxel_View* view = AddComponent(g_ecs, Voxel_View, entity);
  view->grid = CreateEntity(g_ecs);
  Voxel_Grid* grid = AddComponent(g_ecs, Voxel_Grid, view->grid);
  AllocateVoxelGrid(g_vox_allocator, grid, width, height, depth);
  if (num == 3) {
    grid->palette[1] = PACK_COLOR(240, 240, 240, 255);
  } else {
    int r = atoi(args[3]);
    int g = atoi(args[4]);
    int b = atoi(args[5]);
    grid->palette[1] = PACK_COLOR(r, g, b, 255);
  }
  FillVoxelGrid(grid, 1);
  // don't forget to update hash
  RehashVoxelGrid(grid);
  Transform* transform = AddComponent(g_ecs, Transform, entity);
  transform->rotation = QUAT_IDENTITY();
  if (num < 6) {
    transform->position.x = 0.0f;
    transform->position.y = 0.0f;
    transform->position.z = 0.0f;
    transform->scale = 1.0f;
  } else {
    transform->position.x = strtof(args[6], NULL);
    transform->position.y = strtof(args[7], NULL);
    transform->position.z = strtof(args[8], NULL);
    if (num == 8) {
      transform->scale = strtof(args[9], NULL);
    } else {
      transform->scale = 1.0f;
    }
  }
  AddComponent(g_ecs, OBB, entity);
}

void
CMD_remove_script(uint32_t num, const char** args)
{
  if (num != 1) {
    CMD_ARG_COUNT_MISMATCH("only 1");
  }
  EID entity = atoi(args[0]);
  RemoveComponent(g_ecs, Script, entity);
}

void
CMD_set_voxel_backend(uint32_t num, const char** args)
{
  if (num != 1) {
    CMD_ARG_COUNT_MISMATCH("only 1");
  }

  if (strcmp(args[0], "indirect") == 0) {
    SetVoxelBackend_Indirect(g_vox_drawer, g_deletion_queue);
  } else if (strcmp(args[0], "classic") == 0) {
    SetVoxelBackend_Slow(g_vox_drawer, g_deletion_queue);
  } else {
    LOG_WARN("undefined backend '%s'", args[0]);
  }
}

void
CMD_spawn_random_voxels(uint32_t num, const char** args)
{
  if (num != 1) {
    CMD_ARG_COUNT_MISMATCH("only 1");
  }
  enum VOXEL_TYPE {
    VOXEL_TYPE_SPHERE,
    VOXEL_TYPE_CUBE,
    VOXEL_TYPE_FRACTAL1,
    VOXEL_TYPE_FRACTAL2,
    MAX_VOXEL_TYPES,
  };
  int number = atoi(args[0]);
  for (int i = 0; i < number; i++) {
    enum VOXEL_TYPE type = Random(g_random) % MAX_VOXEL_TYPES;
    EID entity = CreateEntity(g_ecs);
    Voxel_View* view = AddComponent(g_ecs, Voxel_View, entity);
    view->grid = CreateEntity(g_ecs);
    Voxel_Grid* grid = AddComponent(g_ecs, Voxel_Grid, view->grid);
    grid->palette[1] = Random(g_random);
    grid->palette[2] = Random(g_random);
    // generate voxel data
    switch (type)
      {
      case VOXEL_TYPE_SPHERE:
        {
          int radius = Random(g_random) % 15 + 1;
          AllocateVoxelGrid(g_vox_allocator, grid, radius*2+1, radius*2+1, radius*2+1);
          GenerateVoxelSphere(grid, radius, 1);
        }break;

      case VOXEL_TYPE_CUBE:
        {
          AllocateVoxelGrid(g_vox_allocator, grid, 16, 16, 16);
          FillVoxelGrid(grid, 1);
          int num = Random(g_random)%255;
          while (num--) {
            uint32_t r = Random(g_random);
            // this surely can be written in a shorter way...
            // but I wan't to get job done
            switch (r%6)
              {
              case 0:
                GetInVoxelGrid(grid, r&15, (r>>4)&15, 0) = 2;
                break;
              case 1:
                GetInVoxelGrid(grid, r&15, (r>>4)&15, 15) = 2;
                break;
              case 2:
                GetInVoxelGrid(grid, 0, r&15, (r>>4)&15) = 2;
                break;
              case 3:
                GetInVoxelGrid(grid, 15, r&15, (r>>4)&15) = 2;
                break;
              case 4:
                GetInVoxelGrid(grid, r&15, 0, (r>>4)&15) = 2;
                break;
              case 5:
                GetInVoxelGrid(grid, r&15, 15, (r>>4)&15) = 2;
                break;
              }
          }
        }break;

      case VOXEL_TYPE_FRACTAL1:
        {
          for (uint32_t i = 3; i < 64; i++)
            grid->palette[i] = Random(g_random);
          GenerateFractal1(entity);
        }break;

      case VOXEL_TYPE_FRACTAL2:
        {
          GenerateFractal2(entity, (grid->palette[1] & 1) ? 3 : 4);
        }break;

      default: assert(0);
      }
    RehashVoxelGrid(grid);
    // create transform
    Transform* transform = AddComponent(g_ecs, Transform, entity);
    transform->scale = (float)(Random(g_random)%5+1);
    transform->position.x = (float)(Random(g_random)%63)-36.0f;
    transform->position.y = (float)(Random(g_random)%10)-0.5f;
    transform->position.z = (float)(Random(g_random)%63)-36.0f;
    Vec3 axis;
    axis.x = (float)(Random(g_random)&255);
    axis.y = (float)(Random(g_random)&255);
    axis.z = (float)(Random(g_random)&255);
    Vec3_Normalize(&axis, &axis);
    float angle = (float)(Random(g_random)%628) / 100.0f;
    QuatFromAxisAngle(&axis, angle, &transform->rotation);
    // just add OBB
    AddComponent(g_ecs, OBB, entity);
    LOG_INFO("spawned at [%.3f %.3f %.3f]", transform->position.x, transform->position.y, transform->position.z);
  }
}

void
CMD_print_transform(uint32_t num, const char** args)
{
  if (num != 1) {
    CMD_ARG_COUNT_MISMATCH("only 1");
  }
  EID entity = atoi(args[0]);
  Transform* transform = GetComponent(Transform, entity);
  if (transform == NULL) {
    LOG_WARN("entity %u doesn't have transform component", entity);
    return;
  }
  char buff[128];
  stbsp_sprintf(buff, "position: %.3f %.3f %.3f", transform->position.x, transform->position.y, transform->position.z);
  ConsolePutLine(buff, PACK_COLOR(100, 100, 255, 255));
  stbsp_sprintf(buff, "rotation: %.3f %.3f %.3f %.3f", transform->rotation.x, transform->rotation.y, transform->rotation.z, transform->rotation.w);
  ConsolePutLine(buff, PACK_COLOR(100, 100, 255, 255));
  stbsp_sprintf(buff, "scale: %.3f", transform->scale);
  ConsolePutLine(buff, PACK_COLOR(100, 100, 255, 255));
}

void
CMD_remove_voxel(uint32_t num, const char** args)
{
  if (num != 1) {
    CMD_ARG_COUNT_MISMATCH("only 1");
  }
  EID entity = atoi(args[0]);
  Voxel_View* cached = GetComponent(Voxel_View, entity);
  RemoveComponent(g_ecs, Voxel_Grid, cached->grid);
  RemoveComponent(g_ecs, Voxel_View, entity);
  RemoveComponent(g_ecs, OBB, entity);
  RemoveComponent(g_ecs, Voxel_View, entity);
  RemoveComponent(g_ecs, Script, entity);
  RemoveComponent(g_ecs, Transform, entity);
}

void
CMD_spawn_random_vox_models(uint32_t num, const char** args)
{
  if (num != 1 && num != 2) {
    CMD_ARG_COUNT_MISMATCH("1 or 2");
  }
  int distrib = 63;
  if (num == 2) {
    distrib = atoi(args[1]);
  }
  const char* left = GetVar_String(g_config, "Misc.vox_models");
  const char* right = left;
  uint32_t count = 0;
  uint32_t offsets[128];
  char buff[1024];
  char* curr = buff;
  while (1) {
    // paths are separated by commas
    if (*right == ',' || *right == '\0') {
      strncpy(curr, left, right-left);
      curr[right-left] = '\0';
      offsets[count++] = curr - buff;
      curr += right-left+1;
      left = right+1;
      if (*right == '\0') break;
    }
    right++;
  }
  int model_count = atoi(args[0]);
  while (model_count--) {
    uint32_t id = Random(g_random) % count;
    EID entity = CreateEntity(g_ecs);
    // load model from file
    LoadVoxModel(g_ecs, g_asset_manager, g_vox_allocator, entity, buff+offsets[id]);
    // create transform
    Transform* transform = AddComponent(g_ecs, Transform, entity);
    transform->scale = (float)(Random(g_random)%5+3);
    transform->position.x = (float)(Random(g_random)%distrib)-distrib*0.5f;
    transform->position.y = (float)(Random(g_random)%(distrib/5))-0.5f;
    transform->position.z = (float)(Random(g_random)%distrib)-distrib*0.5f;
    Vec3 axis;
    axis.x = (float)(Random(g_random)&255);
    axis.y = (float)(Random(g_random)&255);
    axis.z = (float)(Random(g_random)&255);
    Vec3_Normalize(&axis, &axis);
    float angle = (float)(Random(g_random)%628) / 100.0f;
    QuatFromAxisAngle(&axis, angle, &transform->rotation);
    // just add OBB
    AddComponent(g_ecs, OBB, entity);
    // LOG_INFO("spawned '%s' at [%.3f %.3f %.3f]", buff+offsets[id], transform->position.x, transform->position.y, transform->position.z);
  }
}

void
CMD_voxel_buff_statistics(uint32_t num, const char** args)
{
  if (num != 0) {
    CMD_ARG_COUNT_MISMATCH("no");
  }
  (void)args;
  char buff[1024];
  VoxelDrawerStatistics(g_vox_drawer, buff);
  LOG_INFO("%s", buff);
  uint64_t unique_voxels = 0;
  {
    FOREACH_COMPONENT(Voxel_Grid) {
      unique_voxels += VoxelGridBytes(&components[i]);
    }
  }
  uint64_t num_voxels = 0;
  {
    FOREACH_COMPONENT(Voxel_View) {
      Voxel_Grid* grid = GetComponent(Voxel_Grid, components[i].grid);
      num_voxels += VoxelGridBytes(grid);
    }
  }
  LOG_INFO("total voxels: %lu(unique: %lu)", num_voxels, unique_voxels);
}

void CMD_spawn_melon_floor(uint32_t num, const char** args)
{
  if (num > 1) {
    CMD_ARG_COUNT_MISMATCH("0 or 1");
  }
  (void)args;
  EID melon = CreateEntity(g_ecs);
  Voxel_View* view = AddComponent(g_ecs, Voxel_View, melon);
  view->grid = CreateEntity(g_ecs);
  Voxel_Grid* vox = AddComponent(g_ecs, Voxel_Grid, view->grid);
  AllocateVoxelGrid(g_vox_allocator, vox, 128, 4, 128);
  // арбузовое счастье
  vox->palette[1] = 0x00004C00;
  vox->palette[2] = 0x00003C00;
  // NOTE: we can write voxels directly because bounds are divisible by 4
  Voxel* voxels = vox->data->ptr;
  if (num == 0 || strcmp(args[0], "melon") == 0) {
    for (size_t i = 0; i < 32; i++)
      for (size_t j = 0; j < 32; j++) {
        if (i&1) {
          memset(&voxels[(i + j * 32) << 6], 1, 64);
        } else {
          memset(&voxels[(i + j * 32) << 6], 2, 64);
        }
      }
  } else  {
    if (strcmp(args[0], "chess") != 0) {
      LOG_WARN("unrecognised option %s", args[0]);
    }
#if VX_USE_BLOCKS
    for (size_t i = 0; i < 32; i++)
      for (size_t j = 0; j < 32; j++) {
        if ((i+j)&1) {
          memset(&voxels[(i + j * 32) << 6], 2, 64);
        } else {
          memset(&voxels[(i + j * 32) << 6], 1, 64);
        }
      }
#else
    for (size_t i = 0; i < 32; i++)
      for (size_t j = 0; j < 32; j++) {
        Voxel color;
        if ((i+j)&1) {
          color = 2;
        } else {
          color = 1;
        }
        for (size_t z = 0; z < 4; z++)
          for (size_t y = 0; y < 4; y++)
            for (size_t x = 0; x < 4; x++) {
              GetInVoxelGrid(vox, 4*j+x, y, 4*i+z) = color;
            }
      }
#endif
  }
  RehashVoxelGrid(vox);
  Transform* transform = AddComponent(g_ecs, Transform, melon);
  transform->rotation = QUAT_IDENTITY();
  transform->position = VEC3_CREATE(0.0f, -8.0f, 0.0f);
  transform->scale = 4.0f;
  // just add OBB
  AddComponent(g_ecs, OBB, melon);
}
