/* PS/2 keyboard through FabGL. Bindings follow the SDL frontend: arrows and
 * Alt are joystick 0, F2/F3/F4 are OPTION/SELECT/START, F5 resets and F8 is
 * BREAK, F12 opens the supervisor. A PS/2 mouse on port 1 is joystick 0 too:
 * moving it pushes the stick that way, the left button fires, the right is
 * START and the middle SELECT (left and right swap for a left hand). Typing uses FabGL's layout-translated ASCII, so the codes below are
 * the SDL frontend's US-layout codes indexed by character, whichever PS/2
 * layout Setup picked. */
#include "fabgl.h"
#include "hal.h"
#include "../../config.h"
#include "../ui/settings.h"
#include "../ui/supervisor.h"
extern "C" {
#include "../core/Pokey.h"
}

static fabgl::PS2Controller ps2;
static bool altgr; /* right Alt is AltGr, so only left Alt fires */

/* Joystick 0 as the keyboard and the mouse hold it (active low, like PORTA);
 * the Atari sees both ANDed together. */
static uint8_t kbd_stick = 0x0f, kbd_fire = 1;
static uint8_t mouse_stick = 0x0f, mouse_fire = 1;
static int mouse_x, mouse_y; /* stick deflection in mouse counts */
static uint8_t mouse_console; /* console bits the mouse holds down */
static bool mouse_swap;       /* left-handed: right fires, left is START */

/* Layouts from the classic99 port. FabGL reads dead-key tables from the
 * active layout only, so each one leaves them empty: accents are typed
 * directly instead of swallowing the next key. */
using namespace fabgl;

/* Latin America */
static const KeyboardLayout LatamLayout = {
    "LA", "Spanish (Latam)", &USLayout,
    {
        {0x0E, VK_VERTICALBAR},
        {0x4E, VK_QUOTE},
        {0x55, VK_QUESTION_INV},
        {0x54, VK_ACUTEACCENT},
        {0x5B, VK_PLUS},
        {0x4C, VK_TILDE_n},
        {0x52, VK_LEFTBRACE},
        {0x5D, VK_RIGHTBRACE},
        {0x61, VK_LESS},
        {0x4A, VK_MINUS},
    },
    {},
    /* in_key, { CTRL, LALT, RALT, SHIFT }, out_key */
    {
        {VK_VERTICALBAR, {0, 0, 0, 1}, VK_DEGREE},
        {VK_VERTICALBAR, {0, 0, 1, 0}, VK_NEGATION},
        {VK_2, {0, 0, 0, 1}, VK_QUOTEDBL},
        {VK_6, {0, 0, 0, 1}, VK_AMPERSAND},
        {VK_7, {0, 0, 0, 1}, VK_SLASH},
        {VK_8, {0, 0, 0, 1}, VK_LEFTPAREN},
        {VK_9, {0, 0, 0, 1}, VK_RIGHTPAREN},
        {VK_0, {0, 0, 0, 1}, VK_EQUALS},
        {VK_QUOTE, {0, 0, 0, 1}, VK_QUESTION},
        {VK_QUOTE, {0, 0, 1, 0}, VK_BACKSLASH},
        {VK_QUESTION_INV, {0, 0, 0, 1}, VK_EXCLAIM_INV},
        {VK_q, {0, 0, 1, 0}, VK_AT},
        {VK_ACUTEACCENT, {0, 0, 0, 1}, VK_DIAERESIS},
        {VK_PLUS, {0, 0, 0, 1}, VK_ASTERISK},
        {VK_PLUS, {0, 0, 1, 0}, VK_TILDE},
        {VK_TILDE_n, {0, 0, 0, 1}, VK_TILDE_N},
        {VK_LEFTBRACE, {0, 0, 0, 1}, VK_LEFTBRACKET},
        {VK_LEFTBRACE, {0, 0, 1, 0}, VK_CARET},
        {VK_RIGHTBRACE, {0, 0, 0, 1}, VK_RIGHTBRACKET},
        {VK_RIGHTBRACE, {0, 0, 1, 0}, VK_GRAVEACCENT},
        {VK_LESS, {0, 0, 0, 1}, VK_GREATER},
        {VK_COMMA, {0, 0, 0, 1}, VK_SEMICOLON},
        {VK_PERIOD, {0, 0, 0, 1}, VK_COLON},
        {VK_MINUS, {0, 0, 0, 1}, VK_UNDERSCORE},
    },
    {}, {},
};

/* Italian: FabGL's layout plus the accented-letter keys, which carry
 * @ # [ ] { } ^ */
static const KeyboardLayout ItalyLayout = {
    "IT", "Italian", &ItalianLayout,
    {
        {0x54, VK_GRAVE_e},
        {0x4C, VK_GRAVE_o},
        {0x52, VK_GRAVE_a},
        {0x5D, VK_GRAVE_u},
        {0x55, VK_GRAVE_i},
    },
    {},
    {
        {VK_GRAVE_e, {0, 0, 0, 1}, VK_ACUTE_e},
        {VK_GRAVE_e, {0, 0, 1, 0}, VK_LEFTBRACKET},
        {VK_GRAVE_e, {0, 0, 1, 1}, VK_LEFTBRACE},
        {VK_GRAVE_o, {0, 0, 0, 1}, VK_CEDILLA_c},
        {VK_GRAVE_o, {0, 0, 1, 0}, VK_AT},
        {VK_GRAVE_a, {0, 0, 0, 1}, VK_DEGREE},
        {VK_GRAVE_a, {0, 0, 1, 0}, VK_HASH},
        {VK_GRAVE_u, {0, 0, 0, 1}, VK_SECTION},
        {VK_GRAVE_i, {0, 0, 0, 1}, VK_CARET},
    },
    {}, {},
};

/* Portuguese, Brazil ABNT2. The extra "/ ?" key beside right Shift is
 * scancode 0x51; AltGr+Q and AltGr+W give the same characters on keyboards
 * without it. */
static const KeyboardLayout BrazilLayout = {
    "BR", "Portuguese (Brazil)", &USLayout,
    {
        {0x0E, VK_QUOTE},
        {0x54, VK_ACUTEACCENT},
        {0x5B, VK_LEFTBRACKET},
        {0x4C, VK_CEDILLA_c},
        {0x52, VK_TILDE},
        {0x5D, VK_RIGHTBRACKET},
        {0x61, VK_BACKSLASH},
        {0x4A, VK_SEMICOLON},
        {0x51, VK_SLASH},
    },
    {},
    {
        {VK_6, {0, 0, 0, 1}, VK_DIAERESIS},
        {VK_ACUTEACCENT, {0, 0, 0, 1}, VK_GRAVEACCENT},
        {VK_CEDILLA_c, {0, 0, 0, 1}, VK_CEDILLA_C},
        {VK_TILDE, {0, 0, 0, 1}, VK_CARET},
        {VK_q, {0, 0, 1, 0}, VK_SLASH},
        {VK_w, {0, 0, 1, 0}, VK_QUESTION},
    },
    {}, {},
};

/* Indexed by KbdChoice. */
static const KeyboardLayout *const layouts[KBD_COUNT] = {
    &USLayout, &LatamLayout, &ItalyLayout, &BrazilLayout};

static const uint8_t letters[26] = {0x3f, 0x15, 0x12, 0x3a, 0x2a, 0x38, 0x3d,
                                    0x39, 0x0d, 0x01, 0x05, 0x00, 0x25, 0x23,
                                    0x08, 0x0a, 0x2f, 0x28, 0x3e, 0x2d, 0x0b,
                                    0x10, 0x2e, 0x16, 0x2b, 0x17};

static uint8_t ascii_code(uint8_t c) {
  static const char symbols[] = "0123456789)!@#$%^&*( -_=+[];:'\",<.>/?\\|";
  static const uint8_t codes[] = {
      0x32, 0x1f, 0x1e, 0x1a, 0x18, 0x1d, 0x1b, 0x33, 0x35, 0x30, /* 0-9 */
      0x72, 0x5f, 0x75, 0x5a, 0x58, 0x5d, 0x47, 0x5b, 0x07, 0x70, /* ) - ( */
      0x21, 0x0e, 0x4e, 0x0f, 0x06, 0x60, 0x62, 0x02, 0x42, 0x73,
      0x5e, 0x20, 0x36, 0x22, 0x37, 0x26, 0x66, 0x46, 0x4f};
  if (c >= 'a' && c <= 'z')
    return letters[c - 'a'];
  if (c >= 'A' && c <= 'Z')
    return (uint8_t)(letters[c - 'A'] | 0x40);
  const char *p = c ? strchr(symbols, c) : NULL;
  return p ? codes[p - symbols] : 0xff;
}

static uint8_t key_code(const fabgl::VirtualKeyItem &k) {
  switch (k.vk) {
  case fabgl::VK_RETURN:
  case fabgl::VK_KP_ENTER:
    return 0x0c;
  case fabgl::VK_BACKSPACE:
    return 0x34;
  case fabgl::VK_ESCAPE:
    return 0x1c;
  case fabgl::VK_TAB:
    return k.SHIFT ? 0x6c : 0x2c;
  case fabgl::VK_CAPSLOCK:
    return 0x3c;
  default:
    break;
  }
  /* FabGL turns CTRL+letter into a control character; use the key itself. */
  if (k.vk >= fabgl::VK_a && k.vk <= fabgl::VK_z)
    return (uint8_t)(letters[k.vk - fabgl::VK_a] | (k.SHIFT ? 0x40 : 0));
  if (k.vk >= fabgl::VK_A && k.vk <= fabgl::VK_Z)
    return (uint8_t)(letters[k.vk - fabgl::VK_A] | (k.SHIFT ? 0x40 : 0));
  return ascii_code(k.ASCII);
}

static void console_key(Atari *a, uint8_t bit, int down) {
  if (down)
    a->console &= (uint8_t)~bit;
  else
    a->console |= bit;
}

static int key_event(Atari *a, const fabgl::VirtualKeyItem &k) {
  uint8_t bit = 0;
  switch (k.vk) {
  case fabgl::VK_UP:
  case fabgl::VK_KP_UP:
    bit = 1;
    break;
  case fabgl::VK_DOWN:
  case fabgl::VK_KP_DOWN:
    bit = 2;
    break;
  case fabgl::VK_LEFT:
  case fabgl::VK_KP_LEFT:
    bit = 4;
    break;
  case fabgl::VK_RIGHT:
  case fabgl::VK_KP_RIGHT:
    bit = 8;
    break;
  case fabgl::VK_RALT:
    if (altgr)
      return KEY_ACTION_NONE;
    /* fall through */
  case fabgl::VK_LALT:
    kbd_fire = (uint8_t)!k.down;
    return KEY_ACTION_NONE;
  case fabgl::VK_F2:
    console_key(a, 4, k.down);
    return KEY_ACTION_NONE;
  case fabgl::VK_F3:
    console_key(a, 2, k.down);
    return KEY_ACTION_NONE;
  case fabgl::VK_F4:
    console_key(a, 1, k.down);
    return KEY_ACTION_NONE;
  case fabgl::VK_F5:
    return k.down ? KEY_ACTION_RESET : KEY_ACTION_NONE;
  case fabgl::VK_F8:
    if (k.down) {
      a->pokey_irqst &= 0x7f;
      if (a->pokey_irqen & 0x80)
        a->irq = 1;
    }
    return KEY_ACTION_NONE;
  default:
    break;
  }
  if (bit) {
    if (k.down)
      kbd_stick &= (uint8_t)~bit;
    else
      kbd_stick |= bit;
    return KEY_ACTION_NONE;
  }
  uint8_t c = key_code(k);
  if (c != 0xff)
    Pokey_KeyEvent(a, (uint8_t)(c | (k.CTRL ? 0x80 : 0)), k.down);
  return KEY_ACTION_NONE;
}

void Keyboard_Init(void) {
  ps2.begin(fabgl::PS2Preset::KeyboardPort0_MousePort1,
            fabgl::KbdMode::CreateVirtualKeysQueue);
  fabgl::Mouse *m = fabgl::PS2Controller::mouse();
  Serial.printf("a800: PS/2 mouse %s\n",
                m && m->isMouseAvailable() ? "present" : "absent");
}

/* Mouse motion deflects the stick by up to MOUSE_JOY_RANGE counts and springs
 * back by a quarter each frame, so a direction holds while the mouse keeps
 * moving and lets go a few frames after it stops. */
static int spring(int v, int d) {
  v += d;
  if (v > MOUSE_JOY_RANGE)
    v = MOUSE_JOY_RANGE;
  else if (v < -MOUSE_JOY_RANGE)
    v = -MOUSE_JOY_RANGE;
  return v - (v > 0 ? v + 3 : v - 3) / 4; /* rounds away from 0, reaches 0 */
}

/* Console keys change only on a button edge, like the F-keys, so the mouse
 * never releases what the keyboard or the boot OPTION hold pressed. */
static void mouse_console_key(Atari *a, uint8_t bit, bool down) {
  if (down == !!(mouse_console & bit))
    return;
  mouse_console ^= bit;
  console_key(a, bit, down);
}

static void mouse_poll(Atari *a, bool active) {
  fabgl::Mouse *m = fabgl::PS2Controller::mouse();
  int dx = 0, dy = 0;
  bool fire = mouse_fire == 0;
  fabgl::MouseDelta d;
  while (m && m->deltaAvailable() && m->getNextDelta(&d, 0)) {
    dx += d.deltaX;
    dy += d.deltaY; /* positive is up */
    bool primary = mouse_swap ? d.buttons.right : d.buttons.left;
    bool secondary = mouse_swap ? d.buttons.left : d.buttons.right;
    fire = primary;
    if (active) { /* per packet, so a quick click is not lost */
      mouse_console_key(a, 1, secondary);
      mouse_console_key(a, 2, d.buttons.middle);
    }
  }
  if (!active)
    return; /* drained for the menu; Keyboard_ReleaseAll centred the stick */
  mouse_x = spring(mouse_x, dx);
  mouse_y = spring(mouse_y, dy);
  uint8_t s = 0x0f;
  if (mouse_y >= MOUSE_JOY_THRESHOLD)
    s &= (uint8_t)~1;
  else if (mouse_y <= -MOUSE_JOY_THRESHOLD)
    s &= (uint8_t)~2;
  if (mouse_x <= -MOUSE_JOY_THRESHOLD)
    s &= (uint8_t)~4;
  else if (mouse_x >= MOUSE_JOY_THRESHOLD)
    s &= (uint8_t)~8;
  mouse_stick = s;
  mouse_fire = (uint8_t)!fire;
}

static void update_stick(Atari *a) {
  a->joystick = (uint8_t)(0xf0 | (kbd_stick & mouse_stick));
  a->trigger[0] = (uint8_t)(kbd_fire & mouse_fire);
}

void Keyboard_SetLayout(uint8_t layout) {
  if (layout >= KBD_COUNT)
    layout = KBD_US;
  altgr = layout != KBD_US;
  fabgl::Keyboard *kbd = fabgl::PS2Controller::keyboard();
  if (kbd)
    kbd->setLayout(layouts[layout]);
}

void Keyboard_SetMouseSwap(bool swap) { mouse_swap = swap; }

/* F12 down stops the poll, so the keys after it reach the menu once it is
 * open. Its key-up is ignored. */
int Keyboard_Poll(Atari *a) {
  fabgl::Keyboard *kbd = fabgl::PS2Controller::keyboard();
  int action = KEY_ACTION_NONE;
  fabgl::VirtualKeyItem k;
  mouse_poll(a, !Supervisor_Active());
  while (kbd && kbd->virtualKeyAvailable() > 0 && kbd->getNextVirtualKey(&k, 0)) {
    if (Supervisor_Active())
      Supervisor_Key(k.vk, (uint8_t)k.ASCII, k.down);
    else if (k.vk == fabgl::VK_F12) {
      if (k.down) {
        update_stick(a);
        return action | KEY_ACTION_SUPERVISOR;
      }
    } else
      action |= key_event(a, k);
  }
  if (!Supervisor_Active())
    update_stick(a);
  return action;
}

void Keyboard_ReleaseAll(Atari *a) {
  if (!(a->pokey_skstat & 4))
    Pokey_KeyEvent(a, a->keycode, 0);
  kbd_stick = mouse_stick = 0x0f;
  kbd_fire = mouse_fire = 1;
  mouse_x = mouse_y = 0;
  mouse_console = 0;
  a->joystick = 0xff;
  a->trigger[0] = 1;
  a->console = 7;
}
