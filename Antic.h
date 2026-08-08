#ifndef A800_ANTIC_H
#define A800_ANTIC_H
#include "Atari.h"
uint8_t Antic_Read(Atari *a, uint16_t address);
void Antic_Write(Atari *a, uint16_t address, uint8_t value);
void Antic_Frame(Atari *a);
void Antic_ScheduleDlis(Atari *a);
void Antic_CaptureLine(Atari *a);
#endif
