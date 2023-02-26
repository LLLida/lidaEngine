/*
  Builtin console.
 */

typedef struct {

  Keymap keymap;
  float bottom;
  float target_y;
  float open_speed;
  uint32_t bg_color1;
  uint32_t bg_color2;
  uint32_t fg_color1;
  uint32_t fg_color2;
  EID font;
  float open_t;
  uint32_t last_line;
  uint32_t num_lines;
  uint32_t buff_offset;
  char prompt[256];
  char* lines[128];
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

// NOTE: str is copied, no worries
INTERNAL void
ConsolePutLine(const char* str)
{
  size_t len = strlen(str);
  if (g_console->buff_offset + len >= sizeof(g_console->buffer)) {
    LOG_WARN("console buffer is out of space, rewriting from begin...");
    g_console->buff_offset = 0;
  }
  g_console->last_line = (g_console->last_line+1) % ARR_SIZE(g_console->lines);
  g_console->lines[g_console->last_line] = strcpy(g_console->buffer+g_console->buff_offset, str);
  g_console->buff_offset += len+1;
  if (g_console->num_lines < ARR_SIZE(g_console->lines))
    g_console->num_lines++;
}

INTERNAL void
UpdateConsoleState(float dt)
{
  g_console->bg_color1 = PACK_COLOR(80, 12, 15, 200);
  g_console->bg_color2 = PACK_COLOR(100, 32, 35, 240);
  g_console->fg_color1 = PACK_COLOR(255, 255, 255, 255);
  g_console->fg_color2 = PACK_COLOR(240, 230, 200, 255);

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
  const float prompt_height = 0.04f;
  const float char_size = 0.03f;
  // draw quads
  Vec2 pos = {0.0f, g_console->bottom - prompt_height};
  Vec2 size = {1.0f, prompt_height};
  DrawQuad(renderer, &pos, &size, g_console->bg_color1);
  pos.y = 0.0f;
  size.y = g_console->bottom - prompt_height;
  DrawQuad(renderer, &pos, &size, g_console->bg_color2);
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
    char* line = g_console->lines[id % ARR_SIZE(g_console->lines)];
    // LOG_DEBUG("line = %s pos={%f %f}", line, pos.x, pos.y);
    // TODO: change argument orger
    DrawText(renderer, font, line, &size, g_console->fg_color2, &pos);
    pos.y -= char_size;
    count--;
  }
  // draw prompt
}

INTERNAL int
ConsoleKeymap_Pressed(PlatformKeyCode key, void* udata)
{
  switch (key)
    {

    case PlatformKey_ESCAPE:
      HideConsole();
      break;

    case PlatformKey_BACKQUOTE:
      HideConsole();
      if (g_console->bottom > 0.5f) {
        ShowConsole();
      } else {
        ShowConsoleBig();
      }
      break;

    default:
      break;

    }
  return 0;
}

INTERNAL int
ConsoleKeymap_Released(PlatformKeyCode key, void* udata)
{

  return 0;
}

INTERNAL int
ConsoleKeymap_Mouse(int x, int y, int xrel, int yrel, void* udata)
{
  return 0;
}


/// public functions

INTERNAL void
InitConsole()
{
  g_console = PersistentAllocate(sizeof(Console));
  g_console->open_speed = 4.0f;
  g_console->bottom = 0.0f;
  g_console->target_y = 0.0f;
  g_console->last_line = ARR_SIZE(g_console->lines)-1;
  g_console->num_lines = 0;
  g_console->buff_offset = 0;
  g_console->keymap = (Keymap) { ConsoleKeymap_Pressed,
                                 ConsoleKeymap_Released,
                                 ConsoleKeymap_Mouse,
                                 NULL };

}

INTERNAL void
UpdateAndDrawConsole(Quad_Renderer* renderer, float dt)
{
  UpdateConsoleState(dt);
  DrawConsole(renderer);
}
