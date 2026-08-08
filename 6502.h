#ifndef A800_6502_H
#define A800_6502_H
#include "Atari.h"
void Cpu_Reset(Atari *a);
int Cpu_Step(Atari *a);
void Cpu_Nmi(Atari *a);
void Cpu_Irq(Atari *a);
#endif
