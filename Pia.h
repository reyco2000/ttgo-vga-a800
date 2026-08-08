#ifndef A800_PIA_H
#define A800_PIA_H
#include "Atari.h"
uint8_t Pia_Read(Atari *a, uint16_t address);
void Pia_Write(Atari *a, uint16_t address, uint8_t value);
#endif
