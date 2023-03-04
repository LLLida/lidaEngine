/*
  Builtin console.
 */

typedef struct {

  char* ptr;
  uint32_t color;

} Console_Line;

typedef void(*Console_Command_Func)(uint32_t num, char** args);

typedef struct {

  const char* name;
  Console_Command_Func func;
  const char* doc;

} Console_Command;
DECLARE_TYPE(Console_Command);

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
    LOG_WARN("console buffer is out of space, rewriting from begin...");
    g_console->buff_offset = 0;
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
        // collect arguments
        // NOTE: overflow can't happen because prompt's max size is 256
        char buff[512];
        char* words[8];
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
          command->func(num_words-1, words+1);
        }
        // ConsolePutLine(g_console->prompt, 0);
        // clear prompt
        g_console->prompt[0] = '\0';
        g_console->cursor_pos = 0;
      } break;

    default:
      break;

    }
  return 0;
}

INTERNAL int
ConsoleKeymap_Mouse(int x, int y, int xrel, int yrel, void* udata)
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
INTERNAL void CMD_info(uint32_t num, char** args);
INTERNAL void CMD_FPS(uint32_t num, char** args);
INTERNAL void CMD_get(uint32_t num, char** args);
INTERNAL void CMD_set(uint32_t num, char** args);
INTERNAL void CMD_list_vars(uint32_t num, char** args);


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
  g_console->keymap = (Keymap) { ConsoleKeymap_Pressed,
                                 NULL,
                                 ConsoleKeymap_Mouse,
                                 ConsoleKeymap_TextInput,
                                 NULL };
  stbsp_sprintf(g_console->prompt, "I'm so hungry :(");
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

void
CMD_info(uint32_t num, char** args)
{
  if (num != 1) {
    LOG_WARN("command 'info' accepts only 1 argument; for detailed explanation type 'info info'");
    return;
  }
  char* begin = args[0];
  Console_Command* command = FHT_Search(&g_console->env, GET_TYPE_INFO(Console_Command), &begin);
  if (command == 0) {
    LOG_WARN("command '%s' does not exist", begin);
    return;
  }
  char buff[512];
  strncpy(buff, command->doc, sizeof(buff));
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
CMD_FPS(uint32_t num, char** args)
{
  (void)args;
  if (num != 0) {
    LOG_WARN("command 'FPS' accepts no arguments; for detailed explanation type 'info FPS'");
    return;
  }
  LOG_INFO("FPS=%f", g_window->frames_per_second);
}

void
CMD_get(uint32_t num, char** args)
{
  if (num != 1) {
    LOG_WARN("command 'get' accepts only 1 argument; for detailed explanation type 'info get'");
    return;
  }
  // CVar* var = FHT_Search(&g_config->vars, GET_TYPE_INFO(CVar), &args[0]);
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
CMD_set(uint32_t num, char** args)
{
  if (num != 2) {
    LOG_WARN("command 'get' accepts only 2 arguments; for detailed explanation type 'info set'");
    return;
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
CMD_list_vars(uint32_t num, char** args)
{
  if (num > 1) {
    LOG_WARN("command 'list_vars' accepts no arguments; for detailed explanation type 'info list_vars'");
    return;
  }
  if (num == 0) {
    ListVars(g_config, &list_vars_Traverse_Func, NULL);
  } else {
    ListVarsPrefix(g_config, &list_vars_Traverse_Func, args[0], NULL);
  }
}
