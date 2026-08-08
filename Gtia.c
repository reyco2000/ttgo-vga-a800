#include "Gtia.h"

/* PAL-inspired YIQ approximation. Atari color values encode hue in the high
 * nibble and luminance in the low nibble. */
uint32_t Gtia_Color(uint8_t c) {
  static const int cs[16] = {100,  92,  71,  38,  0, -38, -71, -92,
                             -100, -92, -71, -38, 0, 38,  71,  92};
  static const int sn[16] = {0, 38,  71,  92,  100,  92,  71,  38,
                             0, -38, -71, -92, -100, -92, -71, -38};
  int h = c >> 4, y = (c & 14) * 17 + 12, i = cs[h] * 42 / 100,
      q = sn[h] * 42 / 100;
  int r = y + i + q, g = y - i / 3 - q / 2, b = y - i + q;
  if (!h)
    r = g = b = y;
  if (r < 0)
    r = 0;
  if (r > 255)
    r = 255;
  if (g < 0)
    g = 0;
  if (g > 255)
    g = 255;
  if (b < 0)
    b = 0;
  if (b > 255)
    b = 255;
  return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
uint8_t Gtia_Read(Atari *a, uint16_t address) {
  unsigned r = address & 31;
  if (r < 16)
    return a->gtia[r];
  if (r >= 16 && r <= 19)
    return a->trigger;
  if (r == 20)
    return 1; /* PAL */
  if (r == 31)
    return a->console;
  return 0xff;
}
void Gtia_Write(Atari *a, uint16_t address, uint8_t value) {
  unsigned r = address & 31;
  a->gtia[r] = value;
  if (r == 30)
    for (unsigned i = 0; i < 16; i++)
      a->gtia[i] = 0;
}
