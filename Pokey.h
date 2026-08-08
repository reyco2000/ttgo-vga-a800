#ifndef A800_POKEY_H
#define A800_POKEY_H
#include "Atari.h"
uint8_t Pokey_Read(Atari *a, uint16_t address);
void Pokey_Write(Atari *a, uint16_t address, uint8_t value);
void Pokey_Advance(Atari *a, int cycles);
void Pokey_Audio(Atari *a, int16_t *samples, int count, int rate);
#endif
