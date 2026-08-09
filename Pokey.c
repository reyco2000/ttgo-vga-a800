#include "Pokey.h"
#include <string.h>

static unsigned period(const uint8_t *r, int ch) {
  uint8_t ctl = r[8];
  unsigned base = (ctl & 1) ? 114u : 28u;
  if (ch == 1 && (ctl & 0x10)) {
    unsigned n = r[0] | (unsigned)r[2] << 8;
    return (ctl & 0x40) ? n + 7u : (n + 1u) * base;
  }
  if (ch == 3 && (ctl & 8)) {
    unsigned n = r[4] | (unsigned)r[6] << 8;
    return (ctl & 0x20) ? n + 7u : (n + 1u) * base;
  }
  if ((ch == 0 && (ctl & 0x40)) || (ch == 2 && (ctl & 0x20)))
    return (unsigned)r[ch * 2] + 4u;
  return ((unsigned)r[ch * 2] + 1u) * base;
}
static unsigned timer_period(const Atari *a, int ch) {
  return period(a->pokey, ch);
}
static void poly_step(Atari *a) {
  uint32_t p = a->audio_poly4 & 15u,
           b = (uint32_t)(~((p >> 2) ^ (p >> 3)) & 1u);
  a->audio_poly4 = ((p << 1) | b) & 15u;
  p = a->audio_poly5 & 31u;
  b = (uint32_t)(~((p >> 2) ^ (p >> 4)) & 1u);
  a->audio_poly5 = ((p << 1) | b) & 31u;
  p = a->audio_poly9 & 0x1ffu;
  b = (p ^ (p >> 5)) & 1u;
  a->audio_poly9 = (p >> 1) | (b << 8);
  p = a->audio_poly17 & 0x1ffffu;
  b = ((p >> 8) ^ (p >> 13)) & 1u;
  uint32_t old = p & 1u;
  p >>= 1;
  p = (p & 0xff7fu) | (b << 7);
  a->audio_poly17 = (p & 0xffffu) | (old << 16);
}
static void random_step(Atari *a) {
  uint32_t p = a->pokey_random_poly9 & 0x1ffu, b = (p ^ (p >> 5)) & 1u;
  a->pokey_random_poly9 = (p >> 1) | (b << 8);
  p = a->rng & 0x1ffffu;
  b = ((p >> 8) ^ (p >> 13)) & 1u;
  uint32_t old = p & 1u;
  p >>= 1;
  p = (p & 0xff7fu) | (b << 7);
  a->rng = (p & 0xffffu) | (old << 16);
}
static uint8_t random_read(Atari *a) {
  if (a->pokey_skctl & 3) {
    uint64_t elapsed = a->cycles - a->pokey_random_cycle;
    while (elapsed--)
      random_step(a);
    a->pokey_random_cycle = a->cycles;
  }
  return (uint8_t)(((a->pokey[8] & 0x80) ? a->pokey_random_poly9 : a->rng) &
                   0xff);
}
static void audio_event(Atari *a, uint8_t reg, uint8_t value) {
  unsigned next = (a->audio_event_write + 1) % A800_AUDIO_EVENTS;
  if (next == a->audio_event_read)
    a->audio_event_read = (a->audio_event_read + 1) % A800_AUDIO_EVENTS;
  a->audio_events[a->audio_event_write] =
      (AtariAudioEvent){a->cycles, reg, value};
  a->audio_event_write = next;
}

uint8_t Pokey_Read(Atari *a, uint16_t address) {
  unsigned r = address & 15;
  if (r < 8)
    return 0xff;
  if (r == 8)
    return 0xff;
  if (r == 9)
    return a->keycode;
  if (r == 10)
    return random_read(a);
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
    static const uint8_t bits[4] = {1, 2, 0, 4};
    for (int ch = 0; ch < 4; ch++)
      if ((value & bits[ch]) && !a->pokey_timer_count[ch])
        a->pokey_timer_count[ch] = timer_period(a, ch);
    a->irq = (uint8_t)(a->pokey_irqst | ~a->pokey_irqen) != 0xff;
    return;
  }
  if (r == 15) {
    a->pokey_skctl = value;
    if (!(value & 3)) {
      a->rng = a->pokey_random_poly9 = 1;
      a->pokey_random_cycle = a->cycles;
    }
    audio_event(a, 15, value);
    return;
  }
  if (r == 9) {
    audio_event(a, 9, value);
    a->pokey_timer_base = a->cycles;
    for (int ch = 0; ch < 4; ch++)
      a->pokey_timer_count[ch] = timer_period(a, ch);
    return;
  }
  a->pokey[r] = value;
  if (r <= 8)
    audio_event(a, (uint8_t)r, value);
  if (r == 10)
    a->pokey_skstat |= 0xe0;
}
void Pokey_Advance(Atari *a, int cycles) {
  /* Channels 1, 2 and 4 assert their matching IRQST bit on underflow. */
  static const uint8_t bits[4] = {1, 2, 0, 4};
  for (int ch = 0; ch < 4; ch++)
    if (bits[ch] && (a->pokey_irqen & bits[ch])) {
      uint64_t left = a->pokey_timer_count[ch];
      unsigned div = timer_period(a, ch);
      if (!left)
        left = div;
      if ((uint64_t)(unsigned)cycles >= left) {
        a->pokey_irqst &= (uint8_t)~bits[ch];
        uint64_t extra = (uint64_t)(unsigned)cycles - left;
        left = div - (extra % div);
      } else
        left -= (unsigned)cycles;
      a->pokey_timer_count[ch] = left;
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
  if (!a->audio_started) {
    a->audio_started = 1;
    a->audio_skctl = a->pokey_skctl;
    if (a->audio_event_read == a->audio_event_write)
      memcpy(a->audio_regs, a->pokey, 9);
  }
  for (int n = 0; n < count; n++) {
    double sum = 0;
    a->audio_render_cycle += (double)a->cpu_hz / rate;
    while (a->audio_event_read != a->audio_event_write &&
           a->audio_events[a->audio_event_read].cycle <=
               a->audio_render_cycle) {
      AtariAudioEvent e = a->audio_events[a->audio_event_read];
      a->audio_event_read = (a->audio_event_read + 1) % A800_AUDIO_EVENTS;
      if (e.reg < 9)
        a->audio_regs[e.reg] = e.value;
      else if (e.reg == 9) {
        memset(a->audio_phase, 0, sizeof(a->audio_phase));
        memset(a->audio_output, 0, sizeof(a->audio_output));
        memset(a->audio_hp, 0, sizeof(a->audio_hp));
      } else {
        a->audio_skctl = e.value;
        if (!(e.value & 3)) {
          a->audio_poly4 = a->audio_poly5 = a->audio_poly9 = a->audio_poly17 =
              1;
          memset(a->audio_output, 0, sizeof(a->audio_output));
          memset(a->audio_hp, 0, sizeof(a->audio_hp));
        }
      }
    }
    a->audio_poly_phase += (double)a->cpu_hz / rate;
    while (a->audio_poly_phase >= 1) {
      a->audio_poly_phase -= 1;
      if (a->audio_skctl & 3)
        poly_step(a);
    }
    for (int ch = 0; ch < 4; ch++) {
      uint8_t ctl = a->audio_regs[ch * 2 + 1];
      int vol = ctl & 15;
      unsigned divisor = period(a->audio_regs, ch);
      a->audio_phase[ch] += (double)a->cpu_hz / (divisor * rate);
      int joined_low = (ch == 0 && (a->audio_regs[8] & 0x10)) ||
                       (ch == 2 && (a->audio_regs[8] & 8));
      while (a->audio_phase[ch] >= 1) {
        a->audio_phase[ch] -= 1;
        if (joined_low || !(a->audio_skctl & 3))
          continue;
        int clock = (ctl & 0x80) || (a->audio_poly5 & 1);
        if (clock) {
          if (ctl & 0x20)
            a->audio_output[ch] ^= 1;
          else
            a->audio_output[ch] =
                (uint8_t)(((ctl & 0x40) ? a->audio_poly4
                                        : ((a->audio_regs[8] & 0x80)
                                               ? a->audio_poly9
                                               : a->audio_poly17)) &
                          1);
        }
        if (ch == 2 && (a->audio_regs[8] & 4))
          a->audio_hp[0] = a->audio_output[0];
        if (ch == 3 && (a->audio_regs[8] & 2))
          a->audio_hp[1] = a->audio_output[1];
      }
      int signal = joined_low ? 0 : a->audio_output[ch];
      if (ch < 2 && ((a->audio_regs[8] >> (2 - ch)) & 1))
        signal ^= a->audio_hp[ch];
      if (vol) {
        if (ctl & 0x10)
          sum += vol;
        else if ((ctl & 0xa0) == 0xa0 && (double)a->cpu_hz / divisor >= rate)
          sum += vol * .5;
        else if (signal)
          sum += vol;
      }
    }
    sum = 60.0 * sum / (sum + 18.0);
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
