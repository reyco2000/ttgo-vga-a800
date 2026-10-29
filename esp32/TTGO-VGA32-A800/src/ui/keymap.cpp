/* The 800XL key list and the PC key bindings (host-tested), plus their NVS
 * blob on the board. */
#include "keymap.h"
#include <string.h>
#ifdef ARDUINO
#include <Preferences.h>
#endif

/* Measured from a photo of the 800XL: 19-pixel key pitch, 17-pixel caps,
 * every main row 283 wide, the console keys in a column beside them. */
#define K(label, shift, name, kind, code, row, x, w) {label, shift, name, kind, code, row, x, w}
#define P(label, shift, name, code, row, x) K(label, shift, name, KM_POKEY, code, row, x, 17)
const KeymapKey keymap_keys[KEYMAP_COUNT] = {
    P("ESC", NULL, "ESC", 0x1c, 0, 0),
    P("1", "!", "1 !", 0x1f, 0, 19),
    P("2", "\"", "2 \"", 0x1e, 0, 38),
    P("3", "#", "3 #", 0x1a, 0, 57),
    P("4", "$", "4 $", 0x18, 0, 76),
    P("5", "%", "5 %", 0x1d, 0, 95),
    P("6", "&", "6 &", 0x1b, 0, 114),
    P("7", "'", "7 '", 0x33, 0, 133),
    P("8", "@", "8 @", 0x35, 0, 152),
    P("9", "(", "9 (", 0x30, 0, 171),
    P("0", ")", "0 )", 0x32, 0, 190),
    P("<", "CLR", "< CLEAR", 0x36, 0, 209),
    P(">", "INS", "> INSERT", 0x37, 0, 228),
    P("DEL", NULL, "DELETE BACK SPACE", 0x34, 0, 247),
    K("BRK", NULL, "BREAK", KM_BREAK, 0, 0, 266, 17),

    K("TAB", NULL, "TAB", KM_POKEY, 0x2c, 1, 0, 26),
    P("Q", NULL, "Q", 0x2f, 1, 28),
    P("W", NULL, "W", 0x2e, 1, 47),
    P("E", NULL, "E", 0x2a, 1, 66),
    P("R", NULL, "R", 0x28, 1, 85),
    P("T", NULL, "T", 0x2d, 1, 104),
    P("Y", NULL, "Y", 0x2b, 1, 123),
    P("U", NULL, "U", 0x0b, 1, 142),
    P("I", NULL, "I", 0x0d, 1, 161),
    P("O", NULL, "O", 0x08, 1, 180),
    P("P", NULL, "P", 0x0a, 1, 199),
    P("-", "_", "- _", 0x0e, 1, 218),
    P("=", "|", "= |", 0x0f, 1, 237),
    K("RETURN", NULL, "RETURN", KM_POKEY, 0x0c, 1, 256, 27),

    K("CTRL", NULL, "CONTROL", KM_FIXED, 0, 2, 0, 32),
    P("A", NULL, "A", 0x3f, 2, 34),
    P("S", NULL, "S", 0x3e, 2, 53),
    P("D", NULL, "D", 0x3a, 2, 72),
    P("F", NULL, "F", 0x38, 2, 91),
    P("G", NULL, "G", 0x3d, 2, 110),
    P("H", NULL, "H", 0x39, 2, 129),
    P("J", NULL, "J", 0x01, 2, 148),
    P("K", NULL, "K", 0x05, 2, 167),
    P("L", NULL, "L", 0x00, 2, 186),
    P(";", ":", "; :", 0x02, 2, 205),
    P("+", "\\", "+ \\", 0x06, 2, 224),
    P("*", "^", "* ^", 0x07, 2, 243),
    K("CAPS", NULL, "CAPS", KM_POKEY, 0x3c, 2, 262, 21),

    K("SHIFT", NULL, "SHIFT", KM_FIXED, 0, 3, 0, 41),
    P("Z", NULL, "Z", 0x17, 3, 43),
    P("X", NULL, "X", 0x16, 3, 62),
    P("C", NULL, "C", 0x12, 3, 81),
    P("V", NULL, "V", 0x10, 3, 100),
    P("B", NULL, "B", 0x15, 3, 119),
    P("N", NULL, "N", 0x23, 3, 138),
    P("M", NULL, "M", 0x25, 3, 157),
    P(",", "[", ", [", 0x20, 3, 176),
    P(".", "]", ". ]", 0x22, 3, 195),
    P("/", "?", "/ ?", 0x26, 3, 214),
    K("SHIFT", NULL, "SHIFT", KM_FIXED, 0, 3, 233, 31),
    P("", NULL, "Atari (inverse video)", 0x27, 3, 266),

    K("", NULL, "SPACE", KM_POKEY, 0x21, 4, 62, 152),

    K("RESET", NULL, "RESET", KM_FIXED, 0, 0, 289, 34),
    K("OPTION", NULL, "OPTION", KM_CONSOLE, 4, 1, 289, 34),
    K("SELECT", NULL, "SELECT", KM_CONSOLE, 2, 2, 289, 34),
    K("START", NULL, "START", KM_CONSOLE, 1, 3, 289, 34),
    K("HELP", NULL, "HELP", KM_POKEY, 0x11, 4, 289, 34),
};
#undef P
#undef K

static KeymapBinding bindings[KEYMAP_COUNT];

uint16_t Keymap_Scancode(const uint8_t *bytes) {
  uint16_t ext = 0;
  if (*bytes == 0xE1)
    return 0; /* PAUSE */
  if (*bytes == 0xE0)
    ext = 0x100, bytes++;
  if (*bytes == 0xF0)
    bytes++;
  return *bytes ? (uint16_t)(ext | *bytes) : 0;
}

const KeymapBinding *Keymap_Get(int key) { return &bindings[key]; }

void Keymap_Set(int key, uint16_t sc, uint16_t vk) {
  for (int i = 0; i < KEYMAP_COUNT; i++)
    if (bindings[i].sc == sc)
      bindings[i] = KeymapBinding{0, 0};
  bindings[key] = KeymapBinding{sc, vk};
}

void Keymap_Clear(int key) { bindings[key] = KeymapBinding{0, 0}; }

void Keymap_ClearAll(void) { memset(bindings, 0, sizeof bindings); }

int Keymap_Find(uint16_t sc) {
  if (!sc)
    return -1;
  for (int i = 0; i < KEYMAP_COUNT; i++)
    if (bindings[i].sc == sc)
      return i;
  return -1;
}

int Keymap_Mapped(void) {
  int n = 0;
  for (int i = 0; i < KEYMAP_COUNT; i++)
    n += bindings[i].sc != 0;
  return n;
}

#ifdef ARDUINO
void Keymap_Load(void) {
  Preferences p;
  Keymap_ClearAll();
  if (!p.begin("a800", true))
    return;
  if (p.getBytesLength("keymap") == sizeof bindings)
    p.getBytes("keymap", bindings, sizeof bindings);
  p.end();
  for (int i = 0; i < KEYMAP_COUNT; i++)
    if (keymap_keys[i].kind == KM_FIXED)
      Keymap_Clear(i);
}

void Keymap_Save(void) {
  Preferences p;
  if (!p.begin("a800", false))
    return;
  p.putBytes("keymap", bindings, sizeof bindings);
  p.end();
}
#endif
