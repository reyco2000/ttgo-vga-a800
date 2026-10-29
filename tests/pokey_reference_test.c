/* Checks the event-driven POKEY synthesis against the original
 * cycle-by-cycle algorithm, kept here as the reference: random register
 * writes, clock chunks, RANDOM reads and sample drains must produce the
 * same samples and the same internal state. */
#include "Pokey.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Unlike check(), stays active in Release builds. */
#define check(x)                                                               \
  ((x) ? (void)0                                                               \
       : (fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x), exit(1)))

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
static unsigned audio_cycle(Atari *a) {
  if (a->pokey_skctl & 3)
    poly_step(a);
  for (int ch = 0; ch < 4; ch++) {
    uint8_t ctl = a->pokey[ch * 2 + 1];
    if (a->audio_phase[ch] <= 0)
      a->audio_phase[ch] = (int32_t)period(a->pokey, ch);
    if (--a->audio_phase[ch] > 0)
      continue;
    a->audio_phase[ch] = (int32_t)period(a->pokey, ch);
    int joined_low =
        (ch == 0 && (a->pokey[8] & 0x10)) || (ch == 2 && (a->pokey[8] & 8));
    if (joined_low || !(a->pokey_skctl & 3))
      continue;
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
      a->audio_hp[0] = a->audio_output[0];
    if (ch == 3 && (a->pokey[8] & 2))
      a->audio_hp[1] = a->audio_output[1];
  }
  unsigned sum = 0;
  for (int ch = 0; ch < 4; ch++) {
    uint8_t ctl = a->pokey[ch * 2 + 1];
    int vol = ctl & 15, joined_low = (ch == 0 && (a->pokey[8] & 0x10)) ||
                                     (ch == 2 && (a->pokey[8] & 8));
    int signal = joined_low ? 0 : a->audio_output[ch];
    if (ch == 0 && (a->pokey_skctl & 8))
      signal &= a->audio_output[1];
    if (ch < 2 && ((a->pokey[8] >> (2 - ch)) & 1))
      signal ^= a->audio_hp[ch];
    if (vol && ((ctl & 0x10) || signal))
      sum += (unsigned)vol;
  }
  return sum;
}
static void reference_advance(Atari *a, int cycles) {
  /* The original kept the phase in a double; above the CPU clock it then
   * emitted one sample per clock. Capping the rate gives the same samples
   * with a phase that cannot wrap. */
  unsigned rate = (unsigned)a->audio_rate < a->cpu_hz ? (unsigned)a->audio_rate
                                                      : a->cpu_hz;
  for (int i = 0; i < cycles; i++) {
    a->audio_sum += audio_cycle(a);
    a->audio_clocks++;
    a->audio_poly_phase += rate;
    if (a->audio_poly_phase >= a->cpu_hz) {
      a->audio_poly_phase -= a->cpu_hz;
      double sum = (double)a->audio_sum / a->audio_clocks;
      a->audio_sum = 0;
      a->audio_clocks = 0;
      double filtered = sum - a->audio_previous + 0.995 * a->audio_filter;
      a->audio_previous = sum;
      a->audio_filter = filtered;
      if (filtered > 40)
        filtered = 40;
      if (filtered < -40)
        filtered = -40;
      if (a->audio_len < A800_AUDIO_BUFFER)
        a->audio_buffer[a->audio_len++] = (int16_t)(filtered * 500.0);
    }
  }
}
/* Original RANDOM: one step per elapsed cycle. */
static uint8_t reference_random(Atari *a) {
  if (a->pokey_skctl & 3) {
    for (uint64_t n = a->cycles - a->pokey_random_cycle; n; n--) {
      uint32_t p = a->pokey_random_poly9 & 0x1ffu, b = (p ^ (p >> 5)) & 1u;
      a->pokey_random_poly9 = (p >> 1) | (b << 8);
      p = a->rng & 0x1ffffu;
      b = ((p >> 8) ^ (p >> 13)) & 1u;
      uint32_t old = p & 1u;
      p >>= 1;
      p = (p & 0xff7fu) | (b << 7);
      a->rng = (p & 0xffffu) | (old << 16);
    }
    a->pokey_random_cycle = a->cycles;
  }
  return (uint8_t)(((a->pokey[8] & 0x80) ? a->pokey_random_poly9 : a->rng) &
                   0xff);
}

static uint32_t seed = 12345;
static uint32_t next(void) {
  seed = seed * 1103515245u + 12345u;
  return seed >> 8;
}
static void same(const Atari *x, const Atari *y) {
  check(x->audio_len == y->audio_len);
  check(!memcmp(x->audio_buffer, y->audio_buffer,
                 (size_t)x->audio_len * sizeof(*x->audio_buffer)));
  check(!memcmp(x->audio_phase, y->audio_phase, sizeof(x->audio_phase)));
  check(!memcmp(x->audio_output, y->audio_output, sizeof(x->audio_output)));
  check(!memcmp(x->audio_hp, y->audio_hp, sizeof(x->audio_hp)));
  check(x->audio_poly4 == y->audio_poly4 && x->audio_poly5 == y->audio_poly5);
  check(x->audio_poly9 == y->audio_poly9 &&
         x->audio_poly17 == y->audio_poly17);
  check(x->audio_sum == y->audio_sum && x->audio_clocks == y->audio_clocks);
  check(x->audio_poly_phase == y->audio_poly_phase);
}

static Atari ref, fast;
int main(void) {
  static const int rates[] = {15625, 44100, 48000, 7, 1789772};
  long samples = 0;
  for (int run = 0; run < 60; run++) {
    memset(&ref, 0, sizeof(ref));
    ref.cpu_hz = run & 1 ? 1773447u : 1789772u;
    ref.audio_poly4 = ref.audio_poly5 = ref.audio_poly9 = ref.audio_poly17 = 1;
    ref.rng = ref.pokey_random_poly9 = 1;
    ref.audio_rate = rates[run % 5];
    ref.pokey_skctl = 3;
    fast = ref;
    for (int op = 0; op < 400; op++) {
      unsigned kind = next() % 16;
      if (kind < 5) {
        /* Mostly audio registers; sometimes SKCTL, STIMER or IRQEN. */
        static const uint8_t regs[] = {0, 1, 2, 3, 4, 5, 6, 7, 8,
                                       8, 9, 14, 15, 1, 3, 5};
        uint16_t address = (uint16_t)(0xd200 + regs[next() % 16]);
        uint8_t value = (uint8_t)next();
        if ((address & 15) == 15)
          value = (uint8_t)(next() % 4 ? (value | 3) : (value & ~3));
        if ((address & 15) == 0 || (address & 15) == 4)
          value = (uint8_t)(next() % 3 ? value : value & 7);
        Pokey_Write(&ref, address, value);
        Pokey_Write(&fast, address, value);
      } else if (kind < 12) {
        int n = (int)(next() % (next() % 4 ? 120 : 30000)) + 1;
        reference_advance(&ref, n);
        ref.cycles += (uint64_t)n;
        if (next() % 3)
          Pokey_Clock(&fast, n);
        else
          Pokey_Advance(&fast, n);
        fast.cycles += (uint64_t)n;
      } else if (kind < 14) {
        check(reference_random(&ref) == Pokey_Read(&fast, 0xd20a));
      } else {
        Pokey_Flush(&fast);
        same(&ref, &fast);
        int16_t a[A800_AUDIO_BUFFER], b[A800_AUDIO_BUFFER];
        int n = ref.audio_len;
        samples += n;
        memcpy(a, ref.audio_buffer, (size_t)n * sizeof(*a));
        ref.audio_len = 0;
        Pokey_Audio(&fast, b, n, fast.audio_rate);
        check(!memcmp(a, b, (size_t)n * sizeof(*a)));
      }
    }
    Pokey_Flush(&fast);
    same(&ref, &fast);
  }
  /* Silence from power-on (the DC filter is exactly zero: the settled-
   * silence fast path), sound, silence again (the filter decays into the
   * denormals and sticks there, so the slow path runs), and sound once more.
   * One sample per CPU clock keeps the run short. */
  memset(&ref, 0, sizeof(ref));
  ref.cpu_hz = 1789772u;
  ref.audio_rate = 1789772;
  ref.audio_poly4 = ref.audio_poly5 = ref.audio_poly9 = ref.audio_poly17 = 1;
  ref.rng = ref.pokey_random_poly9 = 1;
  ref.pokey_skctl = 3;
  fast = ref;
  static const uint8_t phases[4][2] = {
      {0, 0}, {0x10, 0xaf}, {0, 0}, {0x40, 0x48}};
  for (int phase = 0; phase < 4; phase++) {
    Pokey_Write(&ref, 0xd200, phases[phase][0]);
    Pokey_Write(&fast, 0xd200, phases[phase][0]);
    Pokey_Write(&ref, 0xd201, phases[phase][1]);
    Pokey_Write(&fast, 0xd201, phases[phase][1]);
    for (int chunk = 0; chunk < (phase == 2 ? 200 : 20); chunk++) {
      int n = A800_AUDIO_BUFFER;
      reference_advance(&ref, n);
      Pokey_Clock(&fast, n);
      Pokey_Flush(&fast);
      same(&ref, &fast);
      samples += ref.audio_len;
      ref.audio_len = fast.audio_len = 0;
    }
    if (phase == 0)
      check(fast.audio_filter == 0 && fast.audio_previous == 0);
    if (phase == 2)
      check(fast.audio_filter != 0);
  }
  printf("pokey_reference_test: %ld samples identical\n", samples);
  return 0;
}
