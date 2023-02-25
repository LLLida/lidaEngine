/*
  Builtin console.
 */

typedef enum {

  CONSOLE_CLOSED,
  CONSOLE_SMALL_OPEN,
  CONSOLE_BIG_OPEN

} Console_Flags;

typedef struct {

  Keymap keymap;
  float y;
  uint32_t bg_color1;
  uint32_t bg_color2;
  Console_Flags flags;
  float open_t;
  char prompt[256];
  char* lines[128];
  char buffer[8*1024];

} Console;

GLOBAL Console* g_console;


/// private functions

INTERNAL void
EnableConsole()
{
  g_console->flags = CONSOLE_SMALL_OPEN;
  BindKeymap(&g_console->keymap);
}

INTERNAL void
DisableConsole()
{
  g_console->flags = CONSOLE_CLOSED;
  UnbindKeymap();
}

INTERNAL void
UpdateConsoleState()
{
  g_console->bg_color1 = PACK_COLOR(80, 12, 15, 150);
  g_console->bg_color2 = PACK_COLOR(100, 32, 35, 200);
  g_console->y = 0.3f;
}

INTERNAL void
DrawConsole(Quad_Renderer* renderer)
{
  const float prompt_height = 0.05f;
  Vec2 pos = {0.0f, g_console->y};
  Vec2 size = {1.0f, prompt_height};
  DrawQuad(renderer, &pos, &size, g_console->bg_color1);
  pos.y = 0.0f;
  size.y = g_console->y;
  DrawQuad(renderer, &pos, &size, g_console->bg_color2);
}

INTERNAL void
ConsoleKeymap_Pressed(PlatformKeyCode key, void* udata)
{
  switch (key)
    {

    case PlatformKey_ESCAPE:
      DisableConsole();
      break;


    default:
      break;

    }
}

INTERNAL void
ConsoleKeymap_Released(PlatformKeyCode key, void* udata)
{

}

INTERNAL void
ConsoleKeymap_Mouse(int x, int y, int xrel, int yrel, void* udata)
{

}


/// public functions

INTERNAL void
InitConsole()
{
  g_console = PersistentAllocate(sizeof(Console));
  g_console->keymap = (Keymap) { ConsoleKeymap_Pressed,
                                 ConsoleKeymap_Released,
                                 ConsoleKeymap_Mouse,
                                 NULL };

}

INTERNAL void
UpdateAndDrawConsole(Quad_Renderer* renderer)
{
  if (g_console->flags == CONSOLE_CLOSED)
    return;
  UpdateConsoleState();
  DrawConsole(renderer);
}
