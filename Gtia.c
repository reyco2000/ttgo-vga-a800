#include "Gtia.h"
#include "Palette.h"

const uint32_t *Gtia_Palette(AtariVideoStandard standard) {
  return standard == A800_PAL ? pal_palette : ntsc_palette;
}
uint8_t Gtia_Read(Atari *a, uint16_t address) {
  unsigned r = address & 31;
  if (r < 16)
    return a->gtia_collisions[r];
  if (r >= 16 && r <= 19)
    return a->trigger[r - 16] & a->trigger_latch[r - 16];
  if (r == 20)
    return a->video_standard == A800_PAL ? 0x01 : 0x0f;
  if (r == 31) {
    uint8_t c = a->console;
    if (a->option_override) {
      a->option_override--;
      c &= (uint8_t)~4;
    }
    return c;
  }
  return 0xff;
}
void Gtia_Write(Atari *a, uint16_t address, uint8_t value) {
  unsigned r = address & 31;
  a->gtia[r] = value;
  if (r >= 13 && r <= 17)
    a->pm_latch[r == 17 ? 4 : r - 13] = value;
  if (r == 29 && !(value & 4))
    for (int i = 0; i < 4; i++)
      a->trigger_latch[i] = 1;
  if (r == 30)
    for (unsigned i = 0; i < 16; i++)
      a->gtia_collisions[i] = 0;
}
void Gtia_Frame(Atari *a) {
  if (a->gtia[29] & 4)
    for (int i = 0; i < 4; i++)
      a->trigger_latch[i] &= a->trigger[i];
}
