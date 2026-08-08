#ifndef A800_GTIA_H
#define A800_GTIA_H
#include "Atari.h"
uint8_t Gtia_Read(Atari *a, uint16_t address);
void Gtia_Write(Atari *a, uint16_t address, uint8_t value);
uint32_t Gtia_Color(uint8_t value);
#endif
