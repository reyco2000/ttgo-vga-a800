#ifndef A800_POKEY_H
#define A800_POKEY_H
#include "Atari.h"
uint8_t Pokey_Read(Atari *a, uint16_t address);
void Pokey_Write(Atari *a, uint16_t address, uint8_t value);
/* Pokey_Advance runs timers and audio now; Pokey_Clock runs the timers and
 * defers audio until Pokey_Flush, which register writes, Pokey_Audio and
 * the end of Atari_Frame call. */
void Pokey_Advance(Atari *a, int cycles);
void Pokey_Timers(Atari *a, int cycles);
/* Runs after every instruction, so the common case (no timer IRQ enabled)
 * stays inline: only the IRQ line is re-derived. */
static inline void Pokey_Clock(Atari *a, int cycles) {
  a->audio_pending += (uint32_t)cycles;
  if (a->pokey_irqen & 7)
    Pokey_Timers(a, cycles);
  else
    a->irq = (uint8_t)(a->pokey_irqst | ~a->pokey_irqen) != 0xff;
}
void Pokey_Flush(Atari *a);
void Pokey_Audio(Atari *a, int16_t *samples, int count, int rate);
void Pokey_KeyEvent(Atari *a, uint8_t code, int down);
#endif
