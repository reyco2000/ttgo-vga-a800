#include "Pia.h"

void Pia_Reset(Atari *a) {
  a->pia[0] = a->pia[1] = 0xff;
  a->pia[2] = a->pia[3] = 0;
  a->pia_ddr[0] = a->pia_ddr[1] = 0;
  Atari_InvalidateMemoryMap(a);
}
A800_HOT uint8_t Atari_PortB(const Atari *a) {
  return (uint8_t)((a->pia[1] & a->pia_ddr[1]) | ~a->pia_ddr[1]);
}
uint8_t Pia_Read(Atari *a, uint16_t address) {
  unsigned r = address & 3;
  if (r < 2) {
    if (!(a->pia[r + 2] & 4))
      return a->pia_ddr[r];
    if (r == 0)
      return (uint8_t)((a->pia[0] & a->pia_ddr[0]) |
                       (a->joystick & ~a->pia_ddr[0]));
    return Atari_PortB(a);
  }
  return a->pia[r];
}
void Pia_Write(Atari *a, uint16_t address, uint8_t value) {
  unsigned r = address & 3;
  if (r < 2) {
    if (a->pia[r + 2] & 4)
      a->pia[r] = value;
    else
      a->pia_ddr[r] = value;
    if (r == 1)
      Atari_InvalidateMemoryMap(a); /* PORTB banks ROM in and out */
  } else
    a->pia[r] = value;
}
