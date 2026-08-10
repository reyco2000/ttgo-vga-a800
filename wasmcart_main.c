#define WC_AUDIO_FORMAT_F32
#include "Atari.h"
#include "AtariIo.h"
#include "Pokey.h"
#include "wasmcart.h"
#include "wc_cart.h"
#include <stdlib.h>
#include <string.h>

#define DEFAULT_WIDTH 336
#define DEFAULT_HEIGHT 240
#define MAX_WIDTH 336
#define MAX_HEIGHT 240
#define AUDIO_CAP 4096

WC_CART_BUFFERS;

extern int wc_asset_size(const char *, unsigned)
    __attribute__((import_module("env"), import_name("wc_asset_size")));
extern int wc_load_asset(const char *, unsigned, void *, unsigned)
    __attribute__((import_module("env"), import_name("wc_load_asset")));

static Atari machine;
static uint8_t *xex_data;
static size_t xex_size;
static unsigned boot_frames;
static int basic_available, option_boot_frames;

static uint8_t atari_key(unsigned hid, int shift) {
  static const uint8_t letters[26] = {0x3f, 0x15, 0x12, 0x3a, 0x2a, 0x38, 0x3d,
                                      0x39, 0x0d, 0x01, 0x05, 0x00, 0x25, 0x23,
                                      0x08, 0x0a, 0x2f, 0x28, 0x3e, 0x2d, 0x0b,
                                      0x10, 0x2e, 0x16, 0x2b, 0x17};
  static const uint8_t digits[10] = {0x32, 0x1f, 0x1e, 0x1a, 0x18,
                                     0x1d, 0x1b, 0x33, 0x35, 0x30};
  if (hid >= WC_KEY_A && hid <= WC_KEY_Z)
    return (uint8_t)(letters[hid - WC_KEY_A] | (shift ? 0x40 : 0));
  if (hid >= WC_KEY_1 && hid <= WC_KEY_9)
    return digits[hid - WC_KEY_1 + 1];
  if (hid == WC_KEY_0)
    return digits[0];
  switch (hid) {
  case WC_KEY_SPACE:
    return 0x21;
  case WC_KEY_ENTER:
    return 0x0c;
  case WC_KEY_BACKSPACE:
    return 0x34;
  case WC_KEY_ESCAPE:
    return 0x1c;
  case WC_KEY_TAB:
    return shift ? 0x6c : 0x2c;
  case WC_KEY_MINUS:
    return shift ? 0x4e : 0x0e;
  case WC_KEY_EQUAL:
    return shift ? 0x06 : 0x0f;
  case WC_KEY_LBRACKET:
    return 0x60;
  case WC_KEY_RBRACKET:
    return 0x62;
  case WC_KEY_SEMICOLON:
    return shift ? 0x42 : 0x02;
  case WC_KEY_QUOTE:
    return shift ? 0x5e : 0x73;
  case WC_KEY_COMMA:
    return shift ? 0x36 : 0x20;
  case WC_KEY_PERIOD:
    return shift ? 0x37 : 0x22;
  case WC_KEY_SLASH:
    return shift ? 0x66 : 0x26;
  case WC_KEY_BACKSLASH:
    return shift ? 0x4f : 0x46;
  default:
    return 0xff;
  }
}
static int down(unsigned hid) { return WC_KEY_IS_DOWN(wc_keys, hid); }
static void input(void) {
  uint16_t buttons = wc_pads[0].connected ? wc_pads[0].buttons : 0;
  machine.joystick = 0xff;
  if ((buttons & WC_BTN_UP) || wc_pads[0].left_y < -16384 || down(WC_KEY_UP))
    machine.joystick &= ~1;
  if ((buttons & WC_BTN_DOWN) || wc_pads[0].left_y > 16384 || down(WC_KEY_DOWN))
    machine.joystick &= ~2;
  if ((buttons & WC_BTN_LEFT) || wc_pads[0].left_x < -16384 ||
      down(WC_KEY_LEFT))
    machine.joystick &= ~4;
  if ((buttons & WC_BTN_RIGHT) || wc_pads[0].left_x > 16384 ||
      down(WC_KEY_RIGHT))
    machine.joystick &= ~8;
  machine.trigger[0] =
      (uint8_t)!((buttons & (WC_BTN_A | WC_BTN_B | WC_BTN_X)) ||
                 down(WC_KEY_LALT) || down(WC_KEY_RALT));
  machine.console = 7;
  if (option_boot_frames)
    machine.console &= ~4;
  if ((buttons & WC_BTN_START) || down(WC_KEY_F4))
    machine.console &= ~1;
  if ((buttons & WC_BTN_SELECT) || down(WC_KEY_F3))
    machine.console &= ~2;
  if ((buttons & (WC_BTN_Y | WC_BTN_L | WC_BTN_R)) || down(WC_KEY_F2))
    machine.console &= ~4;
  static uint8_t previous[32];
  int shift = down(WC_KEY_LSHIFT) || down(WC_KEY_RSHIFT),
      control = down(WC_KEY_LCTRL) || down(WC_KEY_RCTRL);
  for (unsigned hid = 0; hid < 256; hid++) {
    int now = down(hid), before = WC_KEY_IS_DOWN(previous, hid);
    if (now == before)
      continue;
    if (hid == WC_KEY_F5 && now) {
      int rate = machine.audio_rate;
      Atari_Reset(&machine);
      option_boot_frames = !basic_available;
      Pokey_Audio(&machine, NULL, 0, rate);
      continue;
    }
    uint8_t code = atari_key(hid, shift);
    if (code == 0xff)
      continue;
    if (control)
      code |= 0x80;
    machine.keycode = code;
    if (now) {
      machine.pokey_skstat &= ~4;
      machine.pokey_irqst &= ~0x40;
      if (machine.pokey_irqen & 0x40)
        machine.irq = 1;
    } else
      machine.pokey_skstat |= 4;
  }
  memcpy(previous, wc_keys, sizeof(previous));
}

WC_EXPORT wc_info_t *wc_get_info(void) {
  WC_FILL_INFO(WC_FLAG_KEYBOARD);
  return &wc_info;
}

WC_EXPORT_INIT void wc_init(void) {
#ifdef WASMCART_BASIC
  const char *basic = "ATARIBAS.ROM";
#else
  const char *basic = NULL;
#endif
  basic_available = basic != NULL;
  option_boot_frames = !basic_available;
  if (Atari_Init(&machine, "ATARIXL.ROM", basic))
    return;
  Atari_SetVideoStandard(&machine, A800_NTSC);
  int rate = wc_host_info.audio_sample_rate
                 ? (int)wc_host_info.audio_sample_rate
                 : 48000;
  Pokey_Audio(&machine, NULL, 0, rate);
  int size = wc_asset_size("game.xex", 8);
  if (size > 0) {
    xex_data = (uint8_t *)malloc((size_t)size);
    if (xex_data &&
        wc_load_asset("game.xex", 8, xex_data, (unsigned)size) == size)
      xex_size = (size_t)size;
    else {
      free(xex_data);
      xex_data = NULL;
    }
  }
}

WC_EXPORT_RENDER void wc_render(void) {
  input();
  Atari_Frame(&machine);
  if (option_boot_frames)
    option_boot_frames--;
  if (xex_data && ++boot_frames == A800_XEX_BOOT_FRAMES) {
    if (!Atari_LoadExecutableData(&machine, xex_data, xex_size))
      Atari_Frame(&machine);
    free(xex_data);
    xex_data = NULL;
  }
  for (int y = 0; y < DEFAULT_HEIGHT; y++)
    memcpy(wc_framebuffer + y * DEFAULT_WIDTH,
           machine.pixels + (y + 11) * A800_WIDTH + 24,
           DEFAULT_WIDTH * sizeof(*wc_framebuffer));
  int16_t samples[512];
  while (machine.audio_len) {
    int n = machine.audio_len > 512 ? 512 : machine.audio_len;
    Pokey_Audio(&machine, samples, n, machine.audio_rate);
    for (int i = 0; i < n; i++) {
      float value = samples[i] / 32768.0f;
      wc_audio_ring[wc_audio_write_cursor * 2] = value;
      wc_audio_ring[wc_audio_write_cursor * 2 + 1] = value;
      wc_audio_write_cursor = (wc_audio_write_cursor + 1) % AUDIO_CAP;
    }
  }
}
