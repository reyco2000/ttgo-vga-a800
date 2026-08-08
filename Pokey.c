#include "Pokey.h"
#include <string.h>

static unsigned timer_period(const Atari *a, int ch) {
  uint8_t ctl = a->pokey[8];
  unsigned base = (ctl & 1) ? 114u : 28u;
  if (ch == 1 && (ctl & 0x10)) {
    unsigned n = a->pokey[0] | (unsigned)a->pokey[2] << 8;
    return (ctl & 0x40) ? n + 7u : (n + 1u) * base;
  }
  if (ch == 3 && (ctl & 8)) {
    unsigned n = a->pokey[4] | (unsigned)a->pokey[6] << 8;
    return (ctl & 0x20) ? n + 7u : (n + 1u) * base;
  }
  if ((ch == 0 && (ctl & 0x40)) || (ch == 2 && (ctl & 0x20)))
    return (unsigned)a->pokey[ch * 2] + 4u;
  return ((unsigned)a->pokey[ch * 2] + 1u) * base;
}
static uint32_t lfsr(uint32_t s, unsigned bits, unsigned tap) {
  uint32_t b = (s ^ (s >> tap)) & 1u;
  return (s >> 1) | (b << (bits - 1));
}

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
    if (!(value & 3)) {
      a->audio_poly4 = a->audio_poly5 = a->audio_poly9 = a->audio_poly17 = 1;
      memset(a->audio_output, 0, sizeof(a->audio_output));
    }
    return;
  }
  if (r == 9) {
    for (int i = 0; i < 4; i++) {
      a->audio_phase[i] = 0;
      a->audio_output[i] = 0;
    }
    a->pokey_timer_base = a->cycles;
    return;
  }
  a->pokey[r] = value;
  if (r == 10)
    a->pokey_skstat |= 0xe0;
}
void Pokey_Advance(Atari *a, int cycles) {
  /* Channels 1, 2 and 4 assert their matching IRQST bit on underflow. */
  static const uint8_t bits[4] = {1, 2, 0, 4};
  for (int ch = 0; ch < 4; ch++)
    if (bits[ch] && (a->pokey_irqen & bits[ch])) {
      uint64_t now = a->cycles - a->pokey_timer_base,
               old = now - (unsigned)cycles;
      unsigned div = timer_period(a, ch);
      if (div && now / div != old / div)
        a->pokey_irqst &= (uint8_t)~bits[ch];
    }
  a->irq = (uint8_t)(a->pokey_irqst | ~a->pokey_irqen) != 0xff;
}
void Pokey_Audio(Atari *a, int16_t *out, int count, int rate) {
  if (rate <= 0)
    return;
  if (!a->audio_poly4) {
    a->audio_poly4 = 1;
    a->audio_poly5 = 1;
    a->audio_poly9 = 1;
    a->audio_poly17 = 1;
  }
  for (int n = 0; n < count; n++) {
    double sum = 0;
    a->audio_poly_phase += (double)a->cpu_hz / rate;
    while (a->audio_poly_phase >= 1) {
      a->audio_poly_phase -= 1;
      a->audio_poly4 = lfsr(a->audio_poly4, 4, 1);
      a->audio_poly5 = lfsr(a->audio_poly5, 5, 2);
      a->audio_poly9 = lfsr(a->audio_poly9, 9, 5);
      a->audio_poly17 = lfsr(a->audio_poly17, 17, 5);
    }
    for (int ch = 0; ch < 4; ch++) {
      uint8_t ctl = a->pokey[ch * 2 + 1];
      int vol = ctl & 15;
      unsigned period = timer_period(a, ch);
      a->audio_phase[ch] += (double)a->cpu_hz / (period * rate);
      while (a->audio_phase[ch] >= 1) {
        a->audio_phase[ch] -= 1;
        int clock = (ctl & 0x80) || (a->audio_poly5 & 1);
        if (clock) {
          if (ctl & 0x20)
            a->audio_output[ch] ^= 1;
          else
            a->audio_output[ch] =
                (uint8_t)(((ctl & 0x40)
                               ? a->audio_poly4
                               : ((a->pokey[8] & 0x80) ? a->audio_poly9
                                                       : a->audio_poly17)) &
                          1);
        }
        if (ch == 2 && (a->pokey[8] & 4))
          a->audio_output[0] = 0;
        if (ch == 3 && (a->pokey[8] & 2))
          a->audio_output[1] = 0;
      }
      if (vol && ((ctl & 0x10) || a->audio_output[ch]))
        sum += vol;
    }
    /* POKEY is a unipolar DAC; remove its DC component before SDL output. */
    double filtered = sum - a->audio_previous + 0.995 * a->audio_filter;
    a->audio_previous = sum;
    a->audio_filter = filtered;
    if (filtered > 40)
      filtered = 40;
    if (filtered < -40)
      filtered = -40;
    out[n] = (int16_t)(filtered * 700.0);
  }
}
