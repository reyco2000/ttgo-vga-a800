#ifndef A800_ANTIC_H
#define A800_ANTIC_H
#include "Atari.h"
uint8_t Antic_Read(Atari *a, uint16_t address);
void Antic_Write(Atari *a, uint16_t address, uint8_t value);
void Antic_Frame(Atari *a);
void Antic_BeginFrame(Atari *a);
void Antic_BeginLine(Atari *a, int line);
int Antic_DmaCycles(const Atari *a);
void Antic_RenderLine(Atari *a, int line);
void Antic_CaptureLine(Atari *a);
#endif
