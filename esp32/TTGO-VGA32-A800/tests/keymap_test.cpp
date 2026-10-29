/* Host tests for the key list and the PC key bindings. */
#include "../src/ui/keymap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void scancodes() {
  const uint8_t a_down[] = {0x1c, 0}, a_up[] = {0xf0, 0x1c, 0};
  const uint8_t up_down[] = {0xe0, 0x75, 0}, up_up[] = {0xe0, 0xf0, 0x75, 0};
  const uint8_t pause[] = {0xe1, 0x14, 0x77, 0xe1, 0xf0, 0x14, 0xf0, 0x77};
  const uint8_t none[] = {0};
  CHECK(Keymap_Scancode(a_down) == 0x1c);
  CHECK(Keymap_Scancode(a_up) == 0x1c);
  CHECK(Keymap_Scancode(up_down) == 0x175);
  CHECK(Keymap_Scancode(up_up) == 0x175);
  CHECK(Keymap_Scancode(pause) == 0);
  CHECK(Keymap_Scancode(none) == 0);
}

static void bindings() {
  Keymap_ClearAll();
  CHECK(Keymap_Mapped() == 0);
  CHECK(Keymap_Find(0x1c) == -1);
  CHECK(Keymap_Find(0) == -1); /* unmapped keys are not found as sc 0 */
  Keymap_Set(5, 0x83, 42);
  CHECK(Keymap_Find(0x83) == 5);
  CHECK(Keymap_Get(5)->vk == 42);
  Keymap_Set(9, 0x83, 42); /* the same PC key moves to the new Atari key */
  CHECK(Keymap_Find(0x83) == 9);
  CHECK(Keymap_Get(5)->sc == 0);
  Keymap_Set(9, 0x0b, 7); /* a new PC key replaces the old one */
  CHECK(Keymap_Find(0x83) == -1);
  Keymap_Set(1, 0x175, 3);
  CHECK(Keymap_Mapped() == 2);
  Keymap_Clear(9);
  CHECK(Keymap_Find(0x0b) == -1 && Keymap_Mapped() == 1);
  Keymap_ClearAll();
  CHECK(Keymap_Mapped() == 0);
}

/* Caps inside the picture, none overlapping in a row, POKEY codes unique. */
static void key_list() {
  bool seen[64] = {};
  for (int i = 0; i < KEYMAP_COUNT; i++) {
    const KeymapKey *k = &keymap_keys[i];
    CHECK(k->name && k->label && k->row < 5);
    CHECK(k->x >= 0 && k->w > 0 && k->x + k->w <= KEYMAP_WIDTH);
    if (k->kind == KM_POKEY) {
      CHECK(k->code < 64 && !seen[k->code]);
      seen[k->code] = true;
    }
    for (int j = 0; j < i; j++) {
      const KeymapKey *o = &keymap_keys[j];
      if (o->row == k->row)
        CHECK(k->x >= o->x + o->w || o->x >= k->x + k->w);
    }
  }
}

int main() {
  scancodes();
  bindings();
  key_list();
  printf("keymap_test OK\n");
  return 0;
}
