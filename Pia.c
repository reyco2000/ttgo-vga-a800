#include "Pia.h"

uint8_t Pia_Read(Atari *a, uint16_t address) {
  unsigned r = address & 3;
  if (r == 0)
    return (a->pia[2] & 4) ? a->joystick : a->pia[0];
  if (r == 1)
    return (a->pia[3] & 4) ? a->pia[1] : 0xff;
  return a->pia[r];
}
void Pia_Write(Atari *a, uint16_t address, uint8_t value) {
  unsigned r = address & 3;
  a->pia[r] = value;
}
