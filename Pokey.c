#include "Pokey.h"
#include <math.h>

uint8_t Pokey_Read(Atari *a, uint16_t address) {
  unsigned r = address & 15;
  if (r < 8)
    return 0xff;
  if (r == 8)
    return 0xff;
  if (r == 9)
    return a->keycode;
  if (r == 10) {
    a->rng = a->rng * 1103515245u + 12345u;
    return (uint8_t)(a->rng >> 16);
  }
  if (r == 13)
    return a->pokey[13];
  if (r == 14)
    return a->pokey_irqst;
  if (r == 15)
    return a->pokey_skstat;
  return 0xff;
}
void Pokey_Write(Atari *a, uint16_t address, uint8_t value) {
  unsigned r = address & 15;
  if (r == 14) {
    a->pokey_irqen = value;
    a->pokey_irqst |= (uint8_t)~value;
    a->irq = (uint8_t)(a->pokey_irqst | ~a->pokey_irqen) != 0xff;
    return;
  }
  if (r == 15) {
    a->pokey_skctl = value;
    return;
  }
  a->pokey[r] = value;
  if (r == 10)
    a->pokey_skstat |= 0xe0;
}
void Pokey_Advance(Atari *a, int cycles) {
  /* Compact timer model: channels 1, 2 and 4 assert their matching IRQST
     bit when the global cycle count crosses a divider boundary. */
  static const uint8_t bits[4] = {1, 2, 0, 4};
  for (int ch = 0; ch < 4; ch++)
    if (bits[ch] && (a->pokey_irqen & bits[ch])) {
      unsigned div = ((unsigned)a->pokey[ch * 2] + 1u) * 28u;
      if (div && a->cycles / div != (a->cycles - (unsigned)cycles) / div)
        a->pokey_irqst &= (uint8_t)~bits[ch];
    }
  a->irq = (uint8_t)(a->pokey_irqst | ~a->pokey_irqen) != 0xff;
}
void Pokey_Audio(Atari *a, int16_t *out, int count, int rate) {
  for (int n = 0; n < count; n++) {
    double sum = 0;
    for (int ch = 0; ch < 4; ch++) {
      uint8_t ctl = a->pokey[ch * 2 + 1];
      int vol = ctl & 15;
      double hz = (double)a->cpu_hz / (28.0 * ((int)a->pokey[ch * 2] + 1));
      a->audio_phase[ch] += hz / rate;
      if (a->audio_phase[ch] >= 1.0)
        a->audio_phase[ch] -= 1.0;
      if (vol && !(ctl & 16))
        sum += (a->audio_phase[ch] < .5 ? 1 : -1) * vol;
      else if (vol)
        sum += vol;
    }
    out[n] = (int16_t)(sum * 350.0);
  }
}
