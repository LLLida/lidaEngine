/*
  Builtin console.
 */

typedef struct {

  char* ptr;
  uint32_t color;

} Console_Line;

typedef struct {

  Keymap keymap;
  float bottom;
  float target_y;
  // recommended value: 5.0
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
  // TODO: calculate size in a way that we render pixel-perfect glyphs
  const float prompt_height = 0.04f;
  const float char_size = 0.03f;
  // draw quads
  Vec2 pos = {0.0f, g_console->bottom - prompt_height};
  Vec2 size = {1.0f, prompt_height};
  DrawQuad(renderer, &pos, &size, g_console->bg_color1, 0);
  pos.y = 0.0f;
  size.y = g_console->bottom - prompt_height;
  DrawQuad(renderer, &pos, &size, g_console->bg_color2, 1);
  // draw lines
  Font* font = GetComponent(Font, g_console->font);
  const float left_pad = 0.01f;
  const float bottom_pad = 0.01f;
  pos.x = left_pad;
  pos.y = g_console->bottom - prompt_height - bottom_pad;
  size.x = char_size;
  size.y = char_size;
  uint32_t count = g_console->num_lines;
  while (count > 0) {
    uint32_t id = g_console->last_line + ARR_SIZE(g_console->lines) - g_console->num_lines + count;
    Console_Line* line = &g_console->lines[id % ARR_SIZE(g_console->lines)];
    // TODO(consistency): change argument orger
    DrawText(renderer, font, line->ptr, &size, line->color, &pos);
    pos.y -= char_size;
    count--;
  }
  // draw cursor
  // Why human text is so complex??? 😵
  char current_char = g_console->prompt[g_console->cursor_pos];
  pos.x = left_pad + font->glyphs[(int)current_char].bearing.x * char_size;
  for (int i = 0; i < g_console->cursor_pos; i++) {
    pos.x += font->glyphs[(int)g_console->prompt[i]].advance.x * char_size;
  }
  pos.y = g_console->bottom - char_size - 0.5f * bottom_pad;
  // Space has size.x = 0, which does not satisfy us
  if (current_char == ' ' || current_char == '\0') {
    // 'X' is big. it definitely fits well
    size.x = font->glyphs['X'].size.x * char_size;
  } else {
    size.x = font->glyphs[(int)current_char].size.x * char_size;
  }
  size.y = char_size;
  DrawQuad(renderer, &pos, &size, g_console->cursor_color1, 0);
  // draw prompt text
  pos.x = left_pad;
  pos.y = g_console->bottom - bottom_pad;
  size.x = char_size;
  size.y = char_size;
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
      ConsolePutLine(g_console->prompt, 0);
      g_console->prompt[0] = '\0';
      g_console->cursor_pos = 0;
      break;

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
  // TODO(feature): color based on level
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


/// public functions

INTERNAL void
InitConsole()
{
  g_console = PersistentAllocate(sizeof(Console));
  g_console->open_speed = 5.0f;
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
}

INTERNAL void
UpdateAndDrawConsole(Quad_Renderer* renderer, float dt)
{
  UpdateConsoleState(dt);
  DrawConsole(renderer);
}
