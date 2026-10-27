#include "Antic.h"
#include "6502.h"
#include "Gtia.h"
#include "Pokey.h"
#include <string.h>

uint8_t Antic_Read(Atari *a, uint16_t address) {
  unsigned r = address & 15;
  if (r == 11)
    return (uint8_t)(a->scanline / 2);
  if (r == 15)
    return a->antic[15];
  return a->antic[r];
}
void Antic_Write(Atari *a, uint16_t address, uint8_t value) {
  unsigned r = address & 15;
  if (r == 10) { /* WSYNC: wait for the following scanline */
    a->line_cycles = 114;
    return;
  }
  if (r == 15) {
    a->antic[15] &= (uint8_t)~value;
    return;
  }
  a->antic[r] = value;
}
void Antic_CaptureLine(Atari *a) {
  int y = a->scanline, one = (a->antic[0] & 0x10) != 0;
  unsigned row = (unsigned)(one ? y : y / 2);
  uint16_t base = (uint16_t)((a->antic[7] & (one ? 0xf8 : 0xfc)) << 8);
  memcpy(a->line_antic[A800_LINE(y)], a->antic, sizeof(a->antic));
  if (a->antic[0] & 8)
    for (int p = 0; p < 4; p++)
      if (!(a->gtia[28] & (1 << p)) || (y & 1))
        a->pm_latch[p] = Atari_ReadFast(
            a, (uint16_t)(base + (one ? 0x400 + p * 0x100 : 0x200 + p * 0x80) +
                          row));
  if ((a->antic[0] & 12) && (!(a->gtia[28] & 0x10) || (y & 1)))
    a->pm_latch[4] =
        Atari_ReadFast(a, (uint16_t)(base + (one ? 0x300 : 0x180) + row));
  memcpy(a->line_pmg[A800_LINE(y)], a->pm_latch, sizeof(a->pm_latch));
}
static int rows_for(int mode) {
  static const uint8_t n[16] = {1, 1, 8, 10, 8, 16, 8, 16,
                                8, 4, 4, 2,  1, 2,  1, 1};
  return n[mode & 15];
}
#define ROW(y) ((size_t)A800_LINE(y) * A800_WIDTH)
/* Where a line is drawn: its pixels, and optionally the per-pixel playfield,
 * player and missile bits that collision detection needs. */
typedef struct {
  uint8_t *pixels;
  uint16_t *hits;
} Target;
/* The span writers run once per pixel; inlining lets constant widths fold. */
A800_INLINE void span(const Target *t, int x, int n, uint8_t c) {
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  uint8_t *p = t->pixels + x;
  if (n >= 16)
    memset(p, c, (size_t)n);
  else
    for (int i = 0; i < n; i++)
      p[i] = c;
}
A800_INLINE void pfspan(const Target *t, int x, int n, uint8_t c,
                        uint8_t mask) {
  span(t, x, n, c);
  if (!mask || !t->hits)
    return;
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++)
    t->hits[x + i] |= mask;
}
A800_INLINE int line_bytes(int mode, int width) {
  if (!width)
    return 0;
  if (mode == 6 || mode == 7)
    return width == 3 ? 24 : width == 2 ? 20 : 16;
  if (mode == 8 || mode == 9)
    return width == 3 ? 12 : width == 2 ? 10 : 8;
  if (mode == 10 || mode == 11 || mode == 12)
    return width == 3 ? 24 : width == 2 ? 20 : 16;
  return width == 3 ? 48 : width == 2 ? 40 : 32;
}
A800_INLINE int fetch_bytes(const AnticLine *l) {
  int scroll = (l->scroll & 0x10) != 0;
  return line_bytes(l->mode, scroll && l->width < 3 ? l->width + 1 : l->width);
}
static uint8_t screen_byte(Atari *a, uint16_t address, int offset) {
  return Atari_ReadFast(
      a, (uint16_t)((address & 0xf000) | ((address + offset) & 0x0fff)));
}
/* ANTIC's playfield DMA for one line: the screen bytes and, in character
 * modes, each character's glyph row, read in the order the drawing used. */
static void fetch_line(Atari *a, AnticLine *l, uint16_t mem) {
  int mode = l->mode, row = l->row, bytes = fetch_bytes(l);
  uint8_t chactl = l->antic[1], chbase = l->antic[9];
  for (int b = 0; b < bytes; b++) {
    uint8_t d = screen_byte(a, mem, b);
    l->data[b] = d;
    if (mode == 2 || mode == 3) {
      int blank = mode == 3 && ((d & 0x60) == 0x60 ? row < 2 : row >= 8),
          glyph = row & 7;
      if (chactl & 4)
        glyph = 7 - glyph;
      uint16_t ch =
          (uint16_t)(((chbase & 0xfc) << 8) | ((d & 127) * 8) + glyph);
      l->glyph[b] = blank ? 0 : Atari_ReadFast(a, ch);
    } else if (mode == 4 || mode == 5) {
      int glyph_row = mode == 5 ? row / 2 : row;
      if (chactl & 4)
        glyph_row = 7 - (glyph_row & 7);
      uint16_t ch = (uint16_t)(((chbase & 0xfc) << 8) | ((d & 127) * 8) +
                               (glyph_row & 7));
      l->glyph[b] = Atari_ReadFast(a, ch);
    } else if (mode == 6 || mode == 7) {
      int glyph_row = mode == 7 ? row / 2 : row;
      if (chactl & 4)
        glyph_row = 7 - (glyph_row & 7);
      uint16_t ch = (uint16_t)(((chbase & 0xfe) << 8) | ((d & 63) * 8) +
                               (glyph_row & 7));
      l->glyph[b] = Atari_ReadFast(a, ch);
    }
  }
}
A800_INLINE void draw_gtia_mode(const Target *t, const AnticLine *l, int x, int b,
                           uint8_t d, uint8_t prior, uint8_t bg,
                           const uint8_t pf[4]) {
  for (int k = 0; k < 2; k++) {
    unsigned v = k ? (d & 15) : (d >> 4);
    uint8_t color = bg, mask = 0;
    if ((prior & 0xc0) == 0x40)
      color = (uint8_t)(bg | v);       /* 16 luminances */
    else if ((prior & 0xc0) == 0x80) { /* nine colors */
      if (v < 4)
        color = l->gtia[18 + v];
      else if (v < 8) {
        color = pf[v - 4];
        mask = (uint8_t)(1 << (v - 4));
      }
    } else { /* 16 hues */
      color = (uint8_t)(bg | (v << 4));
      if (!v)
        color &= 0xf0;
    }
    pfspan(t, x + b * 8 + k * 4, 4, color, mask);
  }
}
/* Eight hi-res pixels (modes 2, 3 and 15): ink is PF2's hue with PF1's
 * luminance, and ink pixels register as playfield 2 for collisions. */
A800_INLINE void hires_byte(const Target *t, int x, uint8_t bits,
                            const uint8_t pf[4]) {
  uint8_t paper = pf[2], ink = (uint8_t)((pf[2] & 0xf0) | (pf[1] & 0x0e));
  if (x < 0 || x + 8 > A800_WIDTH) {
    for (int k = 0; k < 8; k++)
      pfspan(t, x + k, 1, (bits & (128 >> k)) ? ink : paper,
             (bits & (128 >> k)) ? 4 : 0);
    return;
  }
  uint8_t *p = t->pixels + x;
  for (int k = 0; k < 8; k++)
    p[k] = (bits >> (7 - k)) & 1u ? ink : paper;
  if (t->hits)
    for (int k = 0; k < 8; k++)
      t->hits[x + k] |= (uint16_t)(((bits >> (7 - k)) & 1u) << 2);
}
static void draw_mode(const Target *t, const AnticLine *l) {
  int mode = l->mode, width = l->width, scroll = (l->scroll & 0x10) != 0,
      bytes = fetch_bytes(l), visible_bytes = line_bytes(mode, width),
      scale = (mode == 6 || mode == 7) ? 2 : 1,
      pixels_per_byte = (mode == 6 || mode == 7)     ? 16
                        : (mode == 8 || mode == 9)   ? 32
                        : (mode >= 10 && mode <= 12) ? 16
                                                     : 8;
  int visible_left = (A800_WIDTH - visible_bytes * pixels_per_byte) / 2,
      visible_right = A800_WIDTH - visible_left,
      x = (A800_WIDTH - bytes * pixels_per_byte) / 2;
  uint8_t bg = l->colors[4],
          pf[4] = {l->colors[0], l->colors[1], l->colors[2], l->colors[3]},
          prior = l->gtia[27], chactl = l->antic[1];
  if (scroll)
    x += (l->antic[4] & 15) * 2;
  /* Antic_DrawLine has already cleared the line to bg. */
  for (int b = 0; b < bytes; b++) {
    uint8_t d = l->data[b];
    if (mode == 2 || mode == 3) {
      uint8_t bits = l->glyph[b];
      if (d & 128) {
        if (chactl & 1)
          bits = 0;
        if (chactl & 2)
          bits ^= 255;
      }
      if (prior & 0xc0) {
        draw_gtia_mode(t, l, x, b, bits, prior, bg, pf);
        continue;
      }
      hires_byte(t, x + b * 8, bits, pf);
    } else if (mode == 4 || mode == 5) {
      uint8_t bits = l->glyph[b];
      for (int k = 0; k < 4; k++) {
        unsigned v = (bits >> (6 - k * 2)) & 3,
                 pfindex = v == 3 && d & 128 ? 3 : v - 1;
        uint8_t color = v ? pf[pfindex] : bg;
        pfspan(t, x + b * 8 + k * 2, 2, color,
               v ? (uint8_t)(1 << pfindex) : 0);
      }
    } else if (mode == 6 || mode == 7) {
      uint8_t bits = l->glyph[b], ink = pf[d >> 6];
      for (int k = 0; k < 8; k++)
        pfspan(t, x + (b * 8 + k) * scale, scale,
               (bits & (128 >> k)) ? ink : bg,
               (bits & (128 >> k)) ? (uint8_t)(1 << (d >> 6)) : 0);
    } else if (mode == 8) {
      for (int k = 0; k < 4; k++) {
        unsigned v = (d >> (6 - k * 2)) & 3;
        pfspan(t, x + b * 32 + k * 8, 8, v ? pf[v - 1] : bg,
               v ? (uint8_t)(1 << (v - 1)) : 0);
      }
    } else if (mode == 9) {
      for (int k = 0; k < 8; k++)
        pfspan(t, x + b * 32 + k * 4, 4, (d & (128 >> k)) ? pf[0] : bg,
               (d & (128 >> k)) ? 1 : 0);
    } else if (mode == 10) {
      for (int k = 0; k < 4; k++) {
        unsigned v = (d >> (6 - k * 2)) & 3;
        pfspan(t, x + b * 16 + k * 4, 4, v ? pf[v - 1] : bg,
               v ? (uint8_t)(1 << (v - 1)) : 0);
      }
    } else if (mode == 11 || mode == 12) {
      for (int k = 0; k < 8; k++)
        pfspan(t, x + b * 16 + k * 2, 2, (d & (128 >> k)) ? pf[0] : bg,
               (d & (128 >> k)) ? 1 : 0);
    } else if (mode == 13 || mode == 14) {
      for (int k = 0; k < 4; k++) {
        unsigned v = (d >> (6 - k * 2)) & 3;
        pfspan(t, x + b * 8 + k * 2, 2, v ? pf[v - 1] : bg,
               v ? (uint8_t)(1 << (v - 1)) : 0);
      }
    } else if (mode == 15 && (prior & 0xc0))
      draw_gtia_mode(t, l, x, b, d, prior, bg, pf);
    else
      hires_byte(t, x + b * 8, d, pf);
  }
  if (scroll) {
    span(t, 0, visible_left, bg);
    span(t, visible_right, A800_WIDTH - visible_right, bg);
    if (t->hits) {
      memset(t->hits, 0, (size_t)visible_left * sizeof(*t->hits));
      memset(t->hits + visible_right, 0,
             (size_t)(A800_WIDTH - visible_right) * sizeof(*t->hits));
    }
  }
}
static int pm_scale(uint8_t size) {
  static const uint8_t scale[4] = {1, 2, 1, 4};
  return scale[size & 3];
}
A800_HOT static uint8_t priority_color(const AnticLine *l, uint16_t objects) {
  const uint8_t *g = l->gtia;
  uint8_t prior = g[27], pf = objects & 15, p = (objects >> 4) & 15,
          m = (objects >> 8) & 15, out = 0;
  int multi = (prior & 0x20) != 0;
  if (prior & 0x10)
    pf |= m ? 8 : 0;
  else
    p |= m;
  int p01 = (p & 3) != 0, p23 = (p & 12) != 0, pf01 = (pf & 3) != 0,
      pf23 = (pf & 12) != 0, pri01 = (prior & 3) != 0, pri12 = (prior & 6) != 0,
      pri23 = (prior & 12) != 0, pri03 = (prior & 9) != 0;
  int sp[4] = {(p & 1) && !(pf01 && pri23) && !((prior & 4) && pf23),
               (p & 2) && !(pf01 && pri23) && !((prior & 4) && pf23) &&
                   (!(p & 1) || multi),
               (p & 4) && !p01 && !(pf23 && pri12) && !(pf01 && !(prior & 1)),
               (p & 8) && !p01 && !(pf23 && pri12) && !(pf01 && !(prior & 1)) &&
                   (!(p & 4) || multi)};
  int sf3 = (pf & 8) && !(p23 && pri03) && !(p01 && !(prior & 4));
  int sf[4] = {(pf & 1) && !(p23 && (prior & 1)) && !(p01 && pri01) && !sf3,
               (pf & 2) && !(p23 && (prior & 1)) && !(p01 && pri01) && !sf3,
               (pf & 4) && !(p23 && pri03) && !(p01 && !(prior & 4)) && !sf3,
               sf3};
  for (int i = 0; i < 4; i++) {
    if (sp[i])
      out |= g[18 + i];
    if (sf[i])
      out |= l->colors[i];
  }
  return (p || pf) ? out : l->colors[4];
}
static void player_pixel(const Target *t, const AnticLine *l,
                         uint8_t *collisions, int x, int n, int p) {
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++) {
    uint16_t *m = &t->hits[x + i];
    uint8_t players = (uint8_t)(*m >> 4) & 15;
    collisions[4 + p] |= (uint8_t)(*m & 15);
    for (int q = 0; q < 4; q++)
      if (players & (1 << q)) {
        collisions[12 + p] |= (uint8_t)(1 << q);
        collisions[12 + q] |= (uint8_t)(1 << p);
      }
    *m |= (uint16_t)(0x10 << p);
    t->pixels[x + i] = priority_color(l, *m);
  }
}
A800_HOT static void missile_pixel(const Target *t, const AnticLine *l,
                                   uint8_t *collisions, int x, int n, int m) {
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++) {
    uint16_t *v = &t->hits[x + i];
    collisions[m] |= (uint8_t)(*v & 15);
    collisions[8 + m] |= (uint8_t)(*v >> 4) & 15;
    *v |= (uint16_t)(0x100 << m);
    t->pixels[x + i] = priority_color(l, *v);
  }
}
static void draw_pmg(const Target *t, const AnticLine *l,
                     uint8_t *collisions) {
  const uint8_t *g = l->gtia, *pm = l->pmg;
  for (int p = 0; p < 4; p++) {
    uint8_t bits = pm[p];
    int s = pm_scale(g[8 + p]), x = (g[p] - 0x22) * 2 + 4;
    for (int b = 0; b < 8; b++)
      if (bits & (128 >> b))
        player_pixel(t, l, collisions, x + b * 2 * s, 2 * s, p);
  }
  uint8_t bits = pm[4];
  for (int m = 0; m < 4; m++) {
    unsigned pair = (bits >> (m * 2)) & 3;
    int s = pm_scale(g[12] >> (m * 2)), x = (g[4 + m] - 0x22) * 2 + 4;
    if (pair & 2)
      missile_pixel(t, l, collisions, x, 2 * s, m);
    if (pair & 1)
      missile_pixel(t, l, collisions, x + 2 * s, 2 * s, m);
  }
}
A800_HOT void Antic_DrawLine(const AnticLine *l, uint8_t *pixels,
                             uint16_t *hits, uint8_t *collisions) {
  Target t = {pixels, hits};
  if (hits)
    memset(hits, 0, A800_WIDTH * sizeof(*hits));
  span(&t, 0, A800_WIDTH, l->colors[4]);
  if (l->mode)
    draw_mode(&t, l);
  if (l->pmg_area && hits && collisions)
    draw_pmg(&t, l, collisions);
}
void Antic_BeginFrame(Atari *a) {
  a->display_list = (uint16_t)(a->antic[2] | a->antic[3] << 8);
  a->screen_memory = 0;
  a->display_mode = a->display_row = a->display_rows = a->display_scroll =
      a->display_dli = a->display_vscroll_active = a->display_first_line =
          a->display_stopped = a->display_dma_bytes = 0;
  a->display_width = 0;
#ifndef A800_LINE_BUFFER
  memset(a->collision_map, 0,
         sizeof(*a->collision_map) * (size_t)A800_WIDTH * a->frame_lines);
  for (int y = 0; y < a->frame_lines; y++)
    memset(a->pixels + ROW(y), a->line_colors[A800_LINE(y)][4], A800_WIDTH);
#endif
}
void Antic_BeginLine(Atari *a, int y) {
  a->display_dma_bytes = 0;
  if (y >= 8 && y < a->frame_lines - 8 && !a->display_stopped &&
      !a->display_rows && (a->antic[0] & 0x20)) {
    int guard = 0;
    while (!a->display_rows && guard++ < 32) {
      uint8_t ins = Atari_ReadFast(a, a->display_list++), mode = ins & 15;
      a->display_dma_bytes++;
      if (mode == 1) {
        uint16_t target =
            (uint16_t)(Atari_ReadFast(a, a->display_list) |
                       Atari_ReadFast(a, (uint16_t)(a->display_list + 1)) << 8);
        a->display_dma_bytes += 2;
        a->display_list = target;
        if (ins & 64) {
          a->display_stopped = 1;
          break;
        }
        continue;
      }
      int rows = mode ? rows_for(mode) : ((ins >> 4) & 7) + 1,
          vscroll = a->antic[5] & 15;
      a->display_mode = mode;
      a->display_row = 0;
      a->display_scroll = ins & 0x30;
      a->display_dli = (uint8_t)(ins & 0x80);
      a->display_width = (uint8_t)(a->antic[0] & 3);
      a->display_first_line = 1;
      if (mode >= 2 && (ins & 64)) {
        a->screen_memory =
            (uint16_t)(Atari_ReadFast(a, a->display_list) |
                       Atari_ReadFast(a, (uint16_t)(a->display_list + 1)) << 8);
        a->display_list += 2;
        a->display_dma_bytes += 2;
      }
      if (mode >= 2 && (ins & 0x20)) {
        if (!a->display_vscroll_active) {
          int skip = vscroll < rows ? vscroll : rows - 1;
          a->display_row = (uint8_t)skip;
          rows -= skip;
          a->display_vscroll_active = 1;
        }
      } else if (a->display_vscroll_active) {
        rows = vscroll < rows ? vscroll + 1 : rows;
        a->display_vscroll_active = 0;
      }
      a->display_rows = (uint8_t)rows;
    }
  }
  if (a->display_rows == 1 && a->display_dli) {
    a->antic[15] = (uint8_t)((a->antic[15] & ~0x40) | 0x80);
    if (a->antic[14] & 0x80)
      a->nmi = 1;
  }
}
int Antic_DmaCycles(const Atari *a) {
  int cycles = a->display_dma_bytes; /* display-list bytes */
  if (a->antic[0] & 8)
    cycles += 4;
  if (a->antic[0] & 4)
    cycles++;
  if (a->display_rows && a->display_mode && a->display_width) {
    int width = (a->display_scroll & 0x10) && a->display_width < 3
                    ? a->display_width + 1
                    : a->display_width,
        bytes = line_bytes(a->display_mode, width);
    cycles += bytes;
    if (a->display_mode >= 2 && a->display_mode <= 7 && a->display_first_line)
      cycles += bytes;
  }
  return cycles < 104 ? cycles : 104;
}
void Antic_PrepareLine(Atari *a, int y, AnticLine *l) {
  const int i = A800_LINE(y);
  l->y = (int16_t)y;
  l->mode = 0;
  l->pmg_area = y >= 8 && y < 248;
  memcpy(l->colors, a->line_colors[i], sizeof(l->colors));
  memcpy(l->gtia, a->line_gtia[i], sizeof(l->gtia));
  memcpy(l->antic, a->line_antic[i], sizeof(l->antic));
  memcpy(l->pmg, a->line_pmg[i], sizeof(l->pmg));
  if (y < 8 || y >= a->frame_lines - 8 || a->display_stopped ||
      !a->display_rows)
    return;
  if (a->display_mode && a->display_width) {
    l->mode = a->display_mode;
    l->row = a->display_row;
    l->width = a->display_width;
    l->scroll = a->display_scroll;
    fetch_line(a, l, a->screen_memory);
  }
  a->display_row++;
  a->display_rows--;
  a->display_first_line = 0;
  if (!a->display_rows && a->display_mode) {
    int width = (a->display_scroll & 0x10) && a->display_width < 3
                    ? a->display_width + 1
                    : a->display_width;
    a->screen_memory =
        (uint16_t)((a->screen_memory & 0xf000) |
                   ((a->screen_memory + line_bytes(a->display_mode, width)) &
                    0x0fff));
  }
}
A800_HOT void Antic_RenderLine(Atari *a, int y) {
  AnticLine line;
  Antic_PrepareLine(a, y, &line);
  if (a->line_defer && !Antic_LineHasPmg(&line)) {
    a->line_defer(a->line_sink_context, &line);
    return;
  }
  Antic_DrawLine(&line, a->pixels + ROW(y), a->collision_map + ROW(y),
                 a->gtia_collisions);
  if (a->line_sink && y >= 0 && y < a->frame_lines)
    a->line_sink(a->line_sink_context, y, a->pixels + ROW(y));
}
void Antic_Frame(Atari *a) {
  Antic_BeginFrame(a);
  for (int y = 0; y < a->frame_lines; y++) {
    Antic_BeginLine(a, y);
    Antic_RenderLine(a, y);
  }
}
