/*

  Input management.

 */

typedef void(*Keyboard_Callback)(PlatformKeyCode key, void* udata);
typedef void(*Mouse_Callback)(int x, int y, int xrel, int yrel, void* udata);

typedef struct {

  Keyboard_Callback on_pressed;
  Keyboard_Callback on_released;
  Mouse_Callback on_mouse;
  void* udata;

} Keymap;

GLOBAL Keymap g_keymap_stack[32];
GLOBAL size_t g_keymap_count;

GLOBAL int modkey_shift;
GLOBAL int modkey_ctrl;
GLOBAL int modkey_alt;


/// public functions

INTERNAL void
BindKeymap(const Keymap* keymap)
{
  if (g_keymap_count == ARR_SIZE(g_keymap_stack)) {
    LOG_WARN("bind_keymap: keymap stack is full");
    return;
  }
  memcpy(&g_keymap_stack[g_keymap_count],  keymap, sizeof(Keymap));
  g_keymap_count++;
}

INTERNAL void
UnbindKeymap()
{
  if (g_keymap_count == 1) {
    LOG_WARN("no keymaps can be unbound");
  }
  g_keymap_count--;
}

INTERNAL void
KeyPressed(PlatformKeyCode key) {
  Assert(g_keymap_count > 0 && "No keymaps are bound");
  // NOTE: we only detect left modifier keys. Should we support right keys?
  // Personally I don't every use them.
  if (key == PlatformKey_LSHIFT) {
    modkey_shift = 1;
  } else if (key == PlatformKey_LCTRL) {
    modkey_ctrl = 1;
  } else if (key == PlatformKey_LALT) {
    modkey_alt = 1;
  }
  Keymap* curr = &g_keymap_stack[g_keymap_count-1];
  if (curr->on_pressed)
    curr->on_pressed(key, curr->udata);
}

INTERNAL void
KeyReleased(PlatformKeyCode key) {
  Assert(g_keymap_count > 0 && "No keymaps are bound");
  if (key == PlatformKey_LSHIFT) {
    modkey_shift = 0;
  } else if (key == PlatformKey_LCTRL) {
    modkey_ctrl = 0;
  } else if (key == PlatformKey_LALT) {
    modkey_alt = 0;
  }
  Keymap* curr = &g_keymap_stack[g_keymap_count-1];
  if (curr->on_released)
    curr->on_released(key, curr->udata);
}

INTERNAL void
MouseMotion(int x, int y, int xrel, int yrel)
{
  Assert(g_keymap_count > 0 && "No keymaps are bound");
  Keymap* curr = &g_keymap_stack[g_keymap_count-1];
  if (curr->on_mouse)
    curr->on_mouse(x, y, xrel, yrel, curr->udata);
}
