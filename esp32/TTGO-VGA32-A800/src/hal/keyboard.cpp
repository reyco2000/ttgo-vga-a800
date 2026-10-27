/* PS/2 keyboard through FabGL. Bindings follow the SDL frontend: arrows and
 * Alt are joystick 0, F2/F3/F4 are OPTION/SELECT/START, F5 resets and F8 is
 * BREAK. Typing uses FabGL's layout-translated ASCII, so the codes below are
 * the SDL frontend's US-layout codes indexed by character. */
#include "fabgl.h"
#include "hal.h"
extern "C" {
#include "../core/Pokey.h"
}

static fabgl::PS2Controller ps2;

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
  case fabgl::VK_LALT:
  case fabgl::VK_RALT:
    a->trigger[0] = (uint8_t)!k.down;
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
      a->joystick &= (uint8_t)~bit;
    else
      a->joystick |= bit;
    return KEY_ACTION_NONE;
  }
  uint8_t c = key_code(k);
  if (c != 0xff)
    Pokey_KeyEvent(a, (uint8_t)(c | (k.CTRL ? 0x80 : 0)), k.down);
  return KEY_ACTION_NONE;
}

void Keyboard_Init(void) {
  ps2.begin(fabgl::PS2Preset::KeyboardPort0, fabgl::KbdMode::CreateVirtualKeysQueue);
}

int Keyboard_Poll(Atari *a) {
  fabgl::Keyboard *kbd = fabgl::PS2Controller::keyboard();
  int action = KEY_ACTION_NONE;
  fabgl::VirtualKeyItem k;
  while (kbd && kbd->virtualKeyAvailable() > 0 && kbd->getNextVirtualKey(&k, 0))
    action |= key_event(a, k);
  return action;
}
