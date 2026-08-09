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
  memcpy(a->line_antic[y], a->antic, sizeof(a->antic));
  if (a->antic[0] & 8)
    for (int p = 0; p < 4; p++)
      if (!(a->gtia[28] & (1 << p)) || (y & 1))
        a->pm_latch[p] = Atari_Read(
            a, (uint16_t)(base + (one ? 0x400 + p * 0x100 : 0x200 + p * 0x80) +
                          row));
  if ((a->antic[0] & 12) && (!(a->gtia[28] & 0x10) || (y & 1)))
    a->pm_latch[4] =
        Atari_Read(a, (uint16_t)(base + (one ? 0x300 : 0x180) + row));
  memcpy(a->line_pmg[y], a->pm_latch, sizeof(a->pm_latch));
}
static int rows_for(int mode) {
  static const uint8_t n[16] = {1, 1, 8, 10, 8, 16, 8, 16,
                                8, 4, 4, 2,  1, 2,  1, 1};
  return n[mode & 15];
}
static void span(Atari *a, int y, int x, int n, uint8_t c) {
  if (y < 0 || y >= a->frame_lines)
    return;
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++)
    a->pixels[y * A800_WIDTH + x + i] = Gtia_Color(c);
}
static void pfspan(Atari *a, int y, int x, int n, uint8_t c, uint8_t mask) {
  span(a, y, x, n, c);
  if (!mask || y < 0 || y >= a->frame_lines)
    return;
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++)
    a->collision_map[y * A800_WIDTH + x + i] |= mask;
}
static int line_bytes(int mode, int width) {
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
static uint8_t screen_byte(Atari *a, uint16_t address, int offset) {
  return Atari_Read(
      a, (uint16_t)((address & 0xf000) | ((address + offset) & 0x0fff)));
}
static void draw_gtia_mode(Atari *a, int y, int x, int b, uint8_t d,
                           uint8_t prior, uint8_t bg, uint8_t pf[4]) {
  uint8_t *g = a->line_gtia[y];
  for (int k = 0; k < 2; k++) {
    unsigned v = k ? (d & 15) : (d >> 4);
    uint8_t color = bg, mask = 0;
    if ((prior & 0xc0) == 0x40)
      color = (uint8_t)(bg | v);       /* 16 luminances */
    else if ((prior & 0xc0) == 0x80) { /* nine colors */
      if (v < 4)
        color = g[18 + v];
      else if (v < 8) {
        color = pf[v - 4];
        mask = (uint8_t)(1 << (v - 4));
      }
    } else { /* 16 hues */
      color = (uint8_t)(bg | (v << 4));
      if (!v)
        color &= 0xf0;
    }
    pfspan(a, y, x + b * 8 + k * 4, 4, color, mask);
  }
}
static void draw_mode(Atari *a, int mode, int y, int row, uint16_t mem,
                      int width) {
  int scroll = (a->display_scroll & 0x10) != 0,
      fetch_width = scroll && width < 3 ? width + 1 : width,
      bytes = line_bytes(mode, fetch_width),
      visible_bytes = line_bytes(mode, width),
      scale = (mode == 6 || mode == 7) ? 2 : 1,
      pixels_per_byte = (mode == 6 || mode == 7)     ? 16
                        : (mode == 8 || mode == 9)   ? 32
                        : (mode >= 10 && mode <= 12) ? 16
                                                     : 8;
  int visible_left = (A800_WIDTH - visible_bytes * pixels_per_byte) / 2,
      visible_right = A800_WIDTH - visible_left,
      x = (A800_WIDTH - bytes * pixels_per_byte) / 2;
  uint8_t bg = a->line_colors[y][4],
          pf[4] = {a->line_colors[y][0], a->line_colors[y][1],
                   a->line_colors[y][2], a->line_colors[y][3]},
          prior = a->line_gtia[y][27], chactl = a->line_antic[y][1];
  if (scroll)
    x += (a->line_antic[y][4] & 15) * 2;
  span(a, y, 0, A800_WIDTH, bg);
  for (int b = 0; b < bytes; b++) {
    uint8_t d = screen_byte(a, mem, b);
    if (mode == 2 || mode == 3) {
      int blank = mode == 3 && ((d & 0x60) == 0x60 ? row < 2 : row >= 8),
          glyph = row & 7;
      if (chactl & 4)
        glyph = 7 - glyph;
      uint16_t ch = (uint16_t)(((a->line_antic[y][9] & 0xfc) << 8) |
                               ((d & 127) * 8) + glyph);
      uint8_t bits = blank ? 0 : Atari_Read(a, ch);
      if (d & 128) {
        if (chactl & 1)
          bits = 0;
        if (chactl & 2)
          bits ^= 255;
      }
      if (prior & 0xc0) {
        draw_gtia_mode(a, y, x, b, bits, prior, bg, pf);
        continue;
      }
      uint8_t paper = pf[2], ink = (uint8_t)((pf[2] & 0xf0) | (pf[1] & 0x0e));
      for (int k = 0; k < 8; k++)
        pfspan(a, y, x + b * 8 + k, 1, (bits & (128 >> k)) ? ink : paper,
               (bits & (128 >> k)) ? 4 : 0);
    } else if (mode == 4 || mode == 5) {
      int glyph_row = mode == 5 ? row / 2 : row;
      if (chactl & 4)
        glyph_row = 7 - (glyph_row & 7);
      uint16_t ch = (uint16_t)(((a->line_antic[y][9] & 0xfc) << 8) |
                               ((d & 127) * 8) + (glyph_row & 7));
      uint8_t bits = Atari_Read(a, ch);
      for (int k = 0; k < 4; k++) {
        unsigned v = (bits >> (6 - k * 2)) & 3,
                 pfindex = v == 3 && d & 128 ? 3 : v - 1;
        uint8_t color = v ? pf[pfindex] : bg;
        pfspan(a, y, x + b * 8 + k * 2, 2, color,
               v ? (uint8_t)(1 << pfindex) : 0);
      }
    } else if (mode == 6 || mode == 7) {
      int glyph_row = mode == 7 ? row / 2 : row;
      if (chactl & 4)
        glyph_row = 7 - (glyph_row & 7);
      uint16_t ch = (uint16_t)(((a->line_antic[y][9] & 0xfe) << 8) |
                               ((d & 63) * 8) + (glyph_row & 7));
      uint8_t bits = Atari_Read(a, ch);
      uint8_t ink = pf[d >> 6];
      for (int k = 0; k < 8; k++)
        pfspan(a, y, x + (b * 8 + k) * scale, scale,
               (bits & (128 >> k)) ? ink : bg,
               (bits & (128 >> k)) ? (uint8_t)(1 << (d >> 6)) : 0);
    } else if (mode == 8) {
      for (int k = 0; k < 4; k++) {
        unsigned v = (d >> (6 - k * 2)) & 3;
        pfspan(a, y, x + b * 32 + k * 8, 8, v ? pf[v - 1] : bg,
               v ? (uint8_t)(1 << (v - 1)) : 0);
      }
    } else if (mode == 9) {
      for (int k = 0; k < 8; k++)
        pfspan(a, y, x + b * 32 + k * 4, 4, (d & (128 >> k)) ? pf[0] : bg,
               (d & (128 >> k)) ? 1 : 0);
    } else if (mode == 10) {
      for (int k = 0; k < 4; k++) {
        unsigned v = (d >> (6 - k * 2)) & 3;
        pfspan(a, y, x + b * 16 + k * 4, 4, v ? pf[v - 1] : bg,
               v ? (uint8_t)(1 << (v - 1)) : 0);
      }
    } else if (mode == 11 || mode == 12) {
      for (int k = 0; k < 8; k++)
        pfspan(a, y, x + b * 16 + k * 2, 2, (d & (128 >> k)) ? pf[0] : bg,
               (d & (128 >> k)) ? 1 : 0);
    } else if (mode == 13 || mode == 14) {
      for (int k = 0; k < 4; k++) {
        unsigned v = (d >> (6 - k * 2)) & 3;
        pfspan(a, y, x + b * 8 + k * 2, 2, v ? pf[v - 1] : bg,
               v ? (uint8_t)(1 << (v - 1)) : 0);
      }
    } else if (mode == 15 && (prior & 0xc0))
      draw_gtia_mode(a, y, x, b, d, prior, bg, pf);
    else {
      uint8_t paper = pf[2], ink = (uint8_t)((pf[2] & 0xf0) | (pf[1] & 0x0e));
      for (int k = 0; k < 8; k++)
        pfspan(a, y, x + b * 8 + k, 1, (d & (128 >> k)) ? ink : paper,
               (d & (128 >> k)) ? 4 : 0);
    }
  }
  if (scroll) {
    span(a, y, 0, visible_left, bg);
    span(a, y, visible_right, A800_WIDTH - visible_right, bg);
    memset(a->collision_map + y * A800_WIDTH, 0,
           (size_t)visible_left * sizeof(*a->collision_map));
    memset(a->collision_map + y * A800_WIDTH + visible_right, 0,
           (size_t)(A800_WIDTH - visible_right) * sizeof(*a->collision_map));
  }
}
static int pm_scale(uint8_t size) {
  static const uint8_t scale[4] = {1, 2, 1, 4};
  return scale[size & 3];
}
static uint8_t priority_color(Atari *a, int y, uint16_t objects) {
  uint8_t *g = a->line_gtia[y], prior = g[27], pf = objects & 15,
          p = (objects >> 4) & 15, m = (objects >> 8) & 15, out = 0;
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
      out |= a->line_colors[y][i];
  }
  return (p || pf) ? out : a->line_colors[y][4];
}
static void player_pixel(Atari *a, int y, int x, int n, int p) {
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++) {
    uint16_t *m = &a->collision_map[y * A800_WIDTH + x + i];
    uint8_t players = (uint8_t)(*m >> 4) & 15;
    a->gtia_collisions[4 + p] |= (uint8_t)(*m & 15);
    for (int q = 0; q < 4; q++)
      if (players & (1 << q)) {
        a->gtia_collisions[12 + p] |= (uint8_t)(1 << q);
        a->gtia_collisions[12 + q] |= (uint8_t)(1 << p);
      }
    *m |= (uint16_t)(0x10 << p);
    a->pixels[y * A800_WIDTH + x + i] = Gtia_Color(priority_color(a, y, *m));
  }
}
static void missile_pixel(Atari *a, int y, int x, int n, int m) {
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++) {
    uint16_t *v = &a->collision_map[y * A800_WIDTH + x + i];
    a->gtia_collisions[m] |= (uint8_t)(*v & 15);
    a->gtia_collisions[8 + m] |= (uint8_t)(*v >> 4) & 15;
    *v |= (uint16_t)(0x100 << m);
    a->pixels[y * A800_WIDTH + x + i] = Gtia_Color(priority_color(a, y, *v));
  }
}
static void draw_pmg(Atari *a, int y) {
  if (y < 8 || y >= 248)
    return;
  uint8_t *g = a->line_gtia[y], *pm = a->line_pmg[y];
  if (g[29] & 2)
    for (int p = 0; p < 4; p++) {
      uint8_t bits = pm[p];
      int s = pm_scale(g[8 + p]), x = (g[p] - 0x22) * 2 + 4;
      for (int b = 0; b < 8; b++)
        if (bits & (128 >> b))
          player_pixel(a, y, x + b * 2 * s, 2 * s, p);
    }
  if (g[29] & 1) {
    uint8_t bits = pm[4];
    for (int m = 0; m < 4; m++) {
      unsigned pair = (bits >> (m * 2)) & 3;
      int s = pm_scale(g[12] >> (m * 2)), x = (g[4 + m] - 0x22) * 2 + 4;
      if (pair & 2)
        missile_pixel(a, y, x, 2 * s, m);
      if (pair & 1)
        missile_pixel(a, y, x + 2 * s, 2 * s, m);
    }
  }
}
void Antic_BeginFrame(Atari *a) {
  a->display_list = (uint16_t)(a->antic[2] | a->antic[3] << 8);
  a->screen_memory = 0;
  a->display_mode = a->display_row = a->display_rows = a->display_scroll =
      a->display_dli = a->display_vscroll_active = a->display_stopped =
          a->display_dma_bytes = 0;
  a->display_width = 0;
  memset(a->collision_map, 0,
         sizeof(*a->collision_map) * (size_t)A800_WIDTH * a->frame_lines);
  for (int y = 0; y < a->frame_lines; y++)
    span(a, y, 0, A800_WIDTH, a->line_colors[y][4]);
}
void Antic_BeginLine(Atari *a, int y) {
  a->display_dma_bytes = 0;
  if (y >= 8 && y < a->frame_lines - 8 && !a->display_stopped &&
      !a->display_rows && (a->antic[0] & 0x20)) {
    int guard = 0;
    while (!a->display_rows && guard++ < 32) {
      uint8_t ins = Atari_Read(a, a->display_list++), mode = ins & 15;
      a->display_dma_bytes++;
      if (mode == 1) {
        uint16_t target =
            (uint16_t)(Atari_Read(a, a->display_list) |
                       Atari_Read(a, (uint16_t)(a->display_list + 1)) << 8);
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
      if (mode >= 2 && (ins & 64)) {
        a->screen_memory =
            (uint16_t)(Atari_Read(a, a->display_list) |
                       Atari_Read(a, (uint16_t)(a->display_list + 1)) << 8);
        a->display_list += 2;
        a->display_dma_bytes += 2;
      }
      if (ins & 0x20) {
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
  int cycles =
      9 + a->display_dma_bytes; /* memory refresh and display-list bytes */
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
    if (a->display_mode >= 2 && a->display_mode <= 7 && a->display_row == 0)
      cycles += bytes;
  }
  return cycles < 104 ? cycles : 104;
}
void Antic_RenderLine(Atari *a, int y) {
  span(a, y, 0, A800_WIDTH, a->line_colors[y][4]);
  if (y >= 8 && y < a->frame_lines - 8 && !a->display_stopped) {
    if (a->display_rows) {
      int row = a->display_row;
      if (a->display_mode && a->display_width)
        draw_mode(a, a->display_mode, y, row, a->screen_memory,
                  a->display_width);
      a->display_row++;
      a->display_rows--;
      if (!a->display_rows && a->display_mode) {
        int width = (a->display_scroll & 0x10) && a->display_width < 3
                        ? a->display_width + 1
                        : a->display_width;
        a->screen_memory = (uint16_t)((a->screen_memory & 0xf000) |
                                      ((a->screen_memory +
                                        line_bytes(a->display_mode, width)) &
                                       0x0fff));
      }
    }
  }
  draw_pmg(a, y);
}
void Antic_Frame(Atari *a) {
  Antic_BeginFrame(a);
  for (int y = 0; y < a->frame_lines; y++) {
    Antic_BeginLine(a, y);
    Antic_RenderLine(a, y);
  }
}
