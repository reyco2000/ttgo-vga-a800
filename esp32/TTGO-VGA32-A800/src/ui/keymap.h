/* Custom key mapping: a PC key can stand for any key of the 800XL keyboard.
 * PC keys are named by PS/2 scancode, so a mapping holds whatever layout
 * Setup picked and with or without SHIFT. The key list follows the 800XL
 * and is also the supervisor's keyboard picture; its order is the NVS blob
 * order. Mappings apply live and are saved in NVS ("a800"/"keymap"). */
#pragma once
#include <stdint.h>

enum KeymapKind : uint8_t {
  KM_FIXED,   /* SHIFT, CONTROL, RESET: cannot be mapped */
  KM_POKEY,   /* code is the POKEY key code; SHIFT and CTRL are added */
  KM_CONSOLE, /* code is the CONSOL bit: 1 START, 2 SELECT, 4 OPTION */
  KM_BREAK
};

struct KeymapKey {
  const char *label; /* legend on the cap */
  const char *shift; /* small legend above it, or NULL */
  const char *name;  /* full name for the supervisor */
  KeymapKind kind;
  uint8_t code;
  uint8_t row;       /* 0..4; the console keys are a column at the right */
  int16_t x, w;      /* pixels inside the picture */
};

enum { KEYMAP_COUNT = 62, KEYMAP_WIDTH = 323 }; /* keys; picture width */
extern const KeymapKey keymap_keys[KEYMAP_COUNT];

/* sc 0 = the key keeps its default PC keys. vk is the FabGL VirtualKey seen
 * when it was captured, kept only to name the PC key. */
struct KeymapBinding {
  uint16_t sc;
  uint16_t vk;
};

/* 0x100 | code for an E0-prefixed key, the code otherwise; 0 for PAUSE or a
 * key with no scancode. bytes is VirtualKeyItem::scancode, down or up. */
uint16_t Keymap_Scancode(const uint8_t *bytes);
const KeymapBinding *Keymap_Get(int key);
/* A PC key stands for one Atari key: mapping it drops it from any other. */
void Keymap_Set(int key, uint16_t sc, uint16_t vk);
void Keymap_Clear(int key);
void Keymap_ClearAll(void);
int Keymap_Find(uint16_t sc); /* Atari key mapped to sc, or -1 */
int Keymap_Mapped(void);      /* how many Atari keys have a PC key */

/* Board only (Preferences). A blob of the wrong size loads as no mappings. */
void Keymap_Load(void);
void Keymap_Save(void);
