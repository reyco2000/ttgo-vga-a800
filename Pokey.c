#include "Pokey.h"
#include <string.h>

A800_HOT static unsigned period(const uint8_t *r, int ch) {
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
static uint32_t poly4_step(uint32_t p) {
  p &= 15u;
  return ((p << 1) | (~((p >> 2) ^ (p >> 3)) & 1u)) & 15u;
}
static uint32_t poly5_step(uint32_t p) {
  p &= 31u;
  return ((p << 1) | (~((p >> 2) ^ (p >> 4)) & 1u)) & 31u;
}
static uint32_t poly9_step(uint32_t p) {
  p &= 0x1ffu;
  return (p >> 1) | (((p ^ (p >> 5)) & 1u) << 8);
}
static uint32_t poly17_step(uint32_t p) {
  p &= 0x1ffffu;
  uint32_t b = ((p >> 8) ^ (p >> 13)) & 1u, old = p & 1u;
  p >>= 1;
  p = (p & 0xff7fu) | (b << 7);
  return (p & 0xffffu) | (old << 16);
}
/* Each polynomial counter is one maximal cycle (15, 31, 511 and 131071
 * states) plus one fixed point, so k steps equal k modulo the cycle length.
 * The 9- and 17-bit counters are linear over GF(2): jump[j][i] holds the
 * image of state bit i after 2^j steps. */
/* jump9 rows use the first 9 columns; both share the [17] row type. */
static uint32_t jump9[9][17], jump17[17][17];
static uint32_t apply(const uint32_t *m, int bits, uint32_t v) {
  uint32_t r = 0;
  for (int i = 0; i < bits; i++)
    if ((v >> i) & 1u)
      r ^= m[i];
  return r;
}
static void init_jumps(void) {
  if (jump17[0][0])
    return;
  for (int i = 0; i < 9; i++)
    jump9[0][i] = poly9_step(1u << i);
  for (int i = 0; i < 17; i++)
    jump17[0][i] = poly17_step(1u << i);
  for (int j = 1; j < 9; j++)
    for (int i = 0; i < 9; i++)
      jump9[j][i] = apply(jump9[j - 1], 9, jump9[j - 1][i]);
  for (int j = 1; j < 17; j++)
    for (int i = 0; i < 17; i++)
      jump17[j][i] = apply(jump17[j - 1], 17, jump17[j - 1][i]);
}
/* poly4 and poly5 by position along their cycle; the all-ones fixed points
 * (15 and 31) stay put. */
static uint8_t seq4[15], pos4[16], seq5[31], pos5[32];
static void init_cycles(void) {
  if (seq4[0])
    return;
  uint32_t p = 1;
  for (int i = 0; i < 15; i++, p = poly4_step(p))
    seq4[i] = (uint8_t)p, pos4[p] = (uint8_t)i;
  p = 1;
  for (int i = 0; i < 31; i++, p = poly5_step(p))
    seq5[i] = (uint8_t)p, pos5[p] = (uint8_t)i;
}
A800_HOT static uint32_t poly4_advance(uint32_t p, uint32_t k) {
  init_cycles();
  p &= 15u;
  return p == 15u ? p : seq4[(pos4[p] + k % 15u) % 15u];
}
A800_HOT static uint32_t poly5_advance(uint32_t p, uint32_t k) {
  init_cycles();
  p &= 31u;
  return p == 31u ? p : seq5[(pos5[p] + k % 31u) % 31u];
}
/* Matrix form of k steps of a linear counter: one product per set bit. */
static uint32_t matrix_advance(uint32_t (*jump)[17], int bits,
                                        uint32_t p, uint32_t k) {
  for (int j = 0; k; j++, k >>= 1)
    if (k & 1u)
      p = apply(jump[j], bits, p);
  return p;
}
/* A constant AUDF asks for the same step count over and over. Once a count
 * repeats, its map is kept as byte-indexed tables: then a jump is two or
 * three lookups instead of up to 17 matrix products. */
typedef struct {
  uint32_t k, last, table[3][256];
  int ready;
} Jump;
static Jump cache9, cache17;
A800_HOT static uint32_t linear_advance(Jump *c, uint32_t (*jump)[17],
                                        int bits, uint32_t p, uint32_t k) {
  if (c->ready && c->k == k)
    return c->table[0][p & 255] ^ c->table[1][(p >> 8) & 255] ^
           c->table[2][(p >> 16) & 255];
  if (k != c->last) {
    c->last = k;
    return matrix_advance(jump, bits, p, k);
  }
  uint32_t column[24] = {0};
  for (int i = 0; i < bits; i++)
    column[i] = matrix_advance(jump, bits, 1u << i, k);
  for (int t = 0; t < 3; t++) {
    c->table[t][0] = 0;
    for (unsigned v = 1; v < 256; v++) {
      int low = 0;
      while (!((v >> low) & 1u))
        low++;
      c->table[t][v] = c->table[t][v & (v - 1)] ^ column[t * 8 + low];
    }
  }
  c->k = k;
  c->ready = 1;
  return linear_advance(c, jump, bits, p, k);
}
A800_HOT static uint32_t poly9_advance(uint32_t p, uint32_t k) {
  init_jumps();
  return linear_advance(&cache9, jump9, 9, p & 0x1ffu,
                        k % 511u);
}
A800_HOT static uint32_t poly17_advance(uint32_t p, uint32_t k) {
  init_jumps();
  return linear_advance(&cache17, jump17, 17, p & 0x1ffffu,
                        k % 131071u);
}
static uint8_t random_read(Atari *a) {
  if (a->pokey_skctl & 3) {
    uint64_t elapsed = a->cycles - a->pokey_random_cycle;
    /* Reduced once here: 64-bit division is a library call on 32-bit
     * targets, and the counters only need the count modulo their cycle. */
    a->pokey_random_poly9 =
        poly9_advance(a->pokey_random_poly9, (uint32_t)(elapsed % 511u));
    a->rng = poly17_advance(a->rng, (uint32_t)(elapsed % 131071u));
    a->pokey_random_cycle = a->cycles;
  }
  return (uint8_t)(((a->pokey[8] & 0x80) ? a->pokey_random_poly9 : a->rng) &
                   0xff);
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
  Pokey_Flush(a);
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
      a->rng = a->pokey_random_poly9 = a->audio_poly4 = a->audio_poly5 =
          a->audio_poly9 = a->audio_poly17 = 1;
      a->pokey_random_cycle = a->cycles;
      memset(a->audio_phase, 0, sizeof(a->audio_phase));
      memset(a->audio_output, 0, sizeof(a->audio_output));
      memset(a->audio_hp, 0, sizeof(a->audio_hp));
      a->audio_len = a->audio_clocks = 0;
      a->audio_sum = 0;
    }
    return;
  }
  if (r == 9) {
    a->pokey_timer_base = a->cycles;
    for (int ch = 0; ch < 4; ch++) {
      a->pokey_timer_count[ch] = timer_period(a, ch);
      a->audio_phase[ch] = (int32_t)timer_period(a, ch);
    }
    a->audio_output[0] = a->audio_output[1] = 0;
    a->audio_output[2] = a->audio_output[3] = 1;
    memset(a->audio_hp, 0, sizeof(a->audio_hp));
    return;
  }
  a->pokey[r] = value;
  if (r == 10)
    a->pokey_skstat |= 0xe0;
}
A800_HOT void Pokey_Timers(Atari *a, int cycles) {
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
/* Mixed volume of the four channels for the current outputs. */
A800_HOT static unsigned audio_level(const Atari *a) {
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
static void emit_sample(Atari *a) {
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
/* Synthesizes `cycles` POKEY clocks with the current registers. Every clock
 * steps the polynomial counters, counts each channel down and adds the mixed
 * level to the running sample; outputs only change when a channel's counter
 * expires. So the loop jumps from one expiry or sample boundary to the next,
 * and the counters catch up (lag) only when a channel samples them. */
A800_HOT static void synthesize(Atari *a, uint32_t cycles) {
  if (a->audio_rate <= 0 || !cycles)
    return;
  const uint8_t *r = a->pokey;
  int poly_on = (a->pokey_skctl & 3) != 0;
  uint32_t lag4 = 0, lag5 = 0, lag9 = 0, lag17 = 0;
  unsigned rate = (unsigned)a->audio_rate, sum = audio_level(a);
  for (int ch = 0; ch < 4; ch++)
    if (a->audio_phase[ch] <= 0)
      a->audio_phase[ch] = (int32_t)period(r, ch);
  while (cycles) {
    uint32_t step = cycles;
    for (int ch = 0; ch < 4; ch++)
      if ((uint32_t)a->audio_phase[ch] < step)
        step = (uint32_t)a->audio_phase[ch];
    uint32_t due = a->audio_poly_phase >= a->cpu_hz
                       ? 1
                       : (a->cpu_hz - a->audio_poly_phase + rate - 1) / rate;
    if (due < step)
      step = due;
    /* step - 1 quiet clocks, then the clock on which something happens. */
    a->audio_sum += sum * (step - 1);
    a->audio_clocks += (int)(step - 1);
    a->audio_poly_phase += rate * (step - 1);
    if (poly_on) {
      lag4 += step;
      lag5 += step;
      lag9 += step;
      lag17 += step;
    }
    int fired = 0;
    for (int ch = 0; ch < 4; ch++) {
      if ((a->audio_phase[ch] -= (int32_t)step) > 0)
        continue;
      a->audio_phase[ch] = (int32_t)period(r, ch);
      fired = 1;
      int joined_low = (ch == 0 && (r[8] & 0x10)) || (ch == 2 && (r[8] & 8));
      if (joined_low || !poly_on)
        continue;
      uint8_t ctl = r[ch * 2 + 1];
      /* Catch up the counter this channel samples at every firing, clocked
       * or not, so the jump length stays the channel period and the jump
       * tables keep hitting. */
      if (!(ctl & 0x20)) {
        if (ctl & 0x40) {
          if (lag4)
            a->audio_poly4 = poly4_advance(a->audio_poly4, lag4), lag4 = 0;
        } else if (r[8] & 0x80) {
          if (lag9)
            a->audio_poly9 = poly9_advance(a->audio_poly9, lag9), lag9 = 0;
        } else if (lag17)
          a->audio_poly17 = poly17_advance(a->audio_poly17, lag17), lag17 = 0;
      }
      if (!(ctl & 0x80) && lag5)
        a->audio_poly5 = poly5_advance(a->audio_poly5, lag5), lag5 = 0;
      if ((ctl & 0x80) || (a->audio_poly5 & 1)) {
        if (ctl & 0x20)
          a->audio_output[ch] ^= 1;
        else
          a->audio_output[ch] =
              (uint8_t)(((ctl & 0x40)     ? a->audio_poly4
                         : (r[8] & 0x80) ? a->audio_poly9
                                          : a->audio_poly17) &
                        1);
      }
      if (ch == 2 && (r[8] & 4))
        a->audio_hp[0] = a->audio_output[0];
      if (ch == 3 && (r[8] & 2))
        a->audio_hp[1] = a->audio_output[1];
    }
    if (fired)
      sum = audio_level(a);
    a->audio_sum += sum;
    a->audio_clocks++;
    a->audio_poly_phase += rate;
    if (a->audio_poly_phase >= a->cpu_hz) {
      a->audio_poly_phase -= a->cpu_hz;
      emit_sample(a);
    }
    cycles -= step;
  }
  a->audio_poly4 = poly4_advance(a->audio_poly4, lag4);
  a->audio_poly5 = poly5_advance(a->audio_poly5, lag5);
  a->audio_poly9 = poly9_advance(a->audio_poly9, lag9);
  a->audio_poly17 = poly17_advance(a->audio_poly17, lag17);
}
A800_HOT void Pokey_Flush(Atari *a) {
  uint32_t pending = a->audio_pending;
  a->audio_pending = 0;
  synthesize(a, pending);
}
A800_HOT void Pokey_Advance(Atari *a, int cycles) {
  Pokey_Timers(a, cycles);
  a->audio_pending += (uint32_t)cycles;
  Pokey_Flush(a);
}
void Pokey_Audio(Atari *a, int16_t *out, int count, int rate) {
  Pokey_Flush(a);
  if (rate <= 0)
    return;
  a->audio_rate = rate;
  if (!out || count <= 0)
    return;
  if (a->audio_poly4 == 0 && a->audio_poly5 == 0 && a->audio_poly9 == 0 &&
      a->audio_poly17 == 0) {
    a->audio_poly4 = 1;
    a->audio_poly5 = 1;
    a->audio_poly9 = 1;
    a->audio_poly17 = 1;
  }
  int n = count < a->audio_len ? count : a->audio_len;
  memcpy(out, a->audio_buffer, (size_t)n * sizeof(*out));
  if (n < a->audio_len)
    memmove(a->audio_buffer, a->audio_buffer + n,
            (size_t)(a->audio_len - n) * sizeof(*out));
  a->audio_len -= n;
  int16_t hold = n ? out[n - 1] : 0;
  while (n < count)
    out[n++] = hold;
}
void Pokey_KeyEvent(Atari *a, uint8_t code, int down) {
  a->keycode = code;
  if (down) {
    a->pokey_skstat &= ~4;
    a->pokey_irqst &= ~0x40;
    if (a->pokey_irqen & 0x40)
      a->irq = 1;
  } else
    a->pokey_skstat |= 4;
}
