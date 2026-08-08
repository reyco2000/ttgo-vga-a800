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
static int rows_for(int mode) {
  static const uint8_t n[16] = {1, 1, 8, 10, 8, 16, 8, 16,
                                8, 4, 4, 2,  1, 2,  1, 1};
  return n[mode & 15];
}
void Antic_ScheduleDlis(Atari *a) {
  uint16_t dl = (uint16_t)(a->antic[2] | a->antic[3] << 8);
  int y = 8, guard = 0;
  memset(a->dli_lines, 0, sizeof(a->dli_lines));
  while (y < a->frame_lines && guard++ < 1024) {
    uint8_t ins = Atari_Read(a, dl++), mode = ins & 15;
    if (mode == 1) {
      dl = (uint16_t)(Atari_Read(a, dl) | Atari_Read(a, (uint16_t)(dl + 1))
                                              << 8);
      if (ins & 64)
        break;
      continue;
    }
    if (mode >= 2 && (ins & 64))
      dl += 2;
    int rows = mode ? rows_for(mode) : ((ins >> 4) & 7) + 1;
    y += rows;
    if ((ins & 128) && y > 0 && y <= a->frame_lines)
      a->dli_lines[y - 1] = 1;
  }
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
  if (mode == 6 || mode == 7)
    return width == 3 ? 24 : width == 2 ? 20 : 16;
  if (mode == 8 || mode == 9)
    return width == 3 ? 12 : width == 2 ? 10 : 8;
  if (mode == 10 || mode == 11 || mode == 12)
    return width == 3 ? 24 : width == 2 ? 20 : 16;
  return width == 3 ? 48 : width == 2 ? 40 : 32;
}
static void draw_mode(Atari *a, int mode, int y, int row, uint16_t mem,
                      int width) {
  int bytes = line_bytes(mode, width), scale = (mode == 6 || mode == 7) ? 2 : 1,
      pixels_per_byte = (mode == 6 || mode == 7)     ? 16
                        : (mode == 8 || mode == 9)   ? 32
                        : (mode >= 10 && mode <= 12) ? 16
                                                     : 8,
      x = (A800_WIDTH - bytes * pixels_per_byte) / 2;
  uint8_t bg = a->line_colors[y][4],
          pf[4] = {a->line_colors[y][0], a->line_colors[y][1],
                   a->line_colors[y][2], a->line_colors[y][3]};
  span(a, y, 0, A800_WIDTH, bg);
  for (int b = 0; b < bytes; b++) {
    uint8_t d = Atari_Read(a, (uint16_t)(mem + b));
    if (mode == 2 || mode == 3) {
      uint16_t ch =
          (uint16_t)(((a->antic[9] & 0xfc) << 8) | ((d & 127) * 8) + (row & 7));
      uint8_t bits = Atari_Read(a, ch);
      if (d & 128)
        bits ^= 255;
      uint8_t paper = pf[2], ink = (uint8_t)((pf[2] & 0xf0) | (pf[1] & 0x0e));
      for (int k = 0; k < 8; k++)
        pfspan(a, y, x + b * 8 + k, 1, (bits & (128 >> k)) ? ink : paper,
               (bits & (128 >> k)) ? 4 : 0);
    } else if (mode == 4 || mode == 5) {
      int glyph_row = mode == 5 ? row / 2 : row;
      uint16_t ch = (uint16_t)(((a->antic[9] & 0xfc) << 8) |
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
      uint16_t ch = (uint16_t)(((a->antic[9] & 0xfe) << 8) |
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
    } else {
      uint8_t paper = pf[2], ink = (uint8_t)((pf[2] & 0xf0) | (pf[1] & 0x0e));
      for (int k = 0; k < 8; k++)
        pfspan(a, y, x + b * 8 + k, 1, (d & (128 >> k)) ? ink : paper,
               (d & (128 >> k)) ? 4 : 0);
    }
  }
}
static int pm_scale(uint8_t size) {
  static const uint8_t scale[4] = {1, 2, 1, 4};
  return scale[size & 3];
}
static void player_pixel(Atari *a, int y, int x, int n, int p, uint8_t color) {
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++) {
    uint8_t *m = &a->collision_map[y * A800_WIDTH + x + i], players = *m >> 4;
    a->gtia_collisions[4 + p] |= *m & 15;
    for (int q = 0; q < 4; q++)
      if (players & (1 << q)) {
        a->gtia_collisions[12 + p] |= (uint8_t)(1 << q);
        a->gtia_collisions[12 + q] |= (uint8_t)(1 << p);
      }
    *m |= (uint8_t)(0x10 << p);
  }
  span(a, y, x, n, color);
}
static void missile_pixel(Atari *a, int y, int x, int n, int m, uint8_t color) {
  if (x < 0) {
    n += x;
    x = 0;
  }
  if (x + n > A800_WIDTH)
    n = A800_WIDTH - x;
  for (int i = 0; i < n; i++) {
    uint8_t v = a->collision_map[y * A800_WIDTH + x + i];
    a->gtia_collisions[m] |= v & 15;
    a->gtia_collisions[8 + m] |= v >> 4;
  }
  span(a, y, x, n, color);
}
static void draw_pmg(Atari *a, int y) {
  if (y < 8 || y >= 248)
    return;
  uint8_t *g = a->line_gtia[y];
  int one = (a->antic[0] & 0x10) != 0;
  uint16_t base = (uint16_t)((a->antic[7] & (one ? 0xf8 : 0xfc)) << 8);
  unsigned row = (unsigned)(one ? y : y / 2);
  if ((a->antic[0] & 8) && (g[29] & 2))
    for (int p = 0; p < 4; p++) {
      uint16_t off =
          (uint16_t)(base + (one ? 0x400 + p * 0x100 : 0x200 + p * 0x80) + row);
      uint8_t bits = Atari_Read(a, off);
      int s = pm_scale(g[8 + p]), x = (g[p] - 0x22) * 2 + 4;
      for (int b = 0; b < 8; b++)
        if (bits & (128 >> b))
          player_pixel(a, y, x + b * 2 * s, 2 * s, p, g[18 + p]);
    }
  if ((a->antic[0] & 4) && (g[29] & 1)) {
    uint16_t off = (uint16_t)(base + (one ? 0x300 : 0x180) + row);
    uint8_t bits = Atari_Read(a, off);
    for (int m = 0; m < 4; m++) {
      unsigned pair = (bits >> (m * 2)) & 3;
      if (pair) {
        int s = pm_scale(g[12] >> (m * 2)), x = (g[4 + m] - 0x22) * 2 + 4;
        missile_pixel(a, y, x, 4 * s, m, g[18 + m]);
      }
    }
  }
}
void Antic_Frame(Atari *a) {
  uint16_t dl = (uint16_t)(a->antic[2] | a->antic[3] << 8), mem = 0;
  int y = 8, guard = 0;
  memset(a->collision_map, 0, (size_t)A800_WIDTH * a->frame_lines);
  for (int y = 0; y < a->frame_lines; y++)
    span(a, y, 0, A800_WIDTH, a->line_colors[y][4]);
  while (y < a->frame_lines - 8 && guard++ < 1024) {
    uint8_t ins = Atari_Read(a, dl++), mode = ins & 15;
    if (mode == 1) {
      uint16_t target = (uint16_t)(Atari_Read(a, dl) |
                                   Atari_Read(a, (uint16_t)(dl + 1)) << 8);
      dl = target;
      if (ins & 64)
        break;
      continue;
    }
    if (mode >= 2 && (ins & 64)) {
      mem = (uint16_t)(Atari_Read(a, dl) | Atari_Read(a, (uint16_t)(dl + 1))
                                               << 8);
      dl += 2;
    }
    int rows = rows_for(mode), width = (a->antic[0] & 3);
    if (!width)
      width = 2;
    if (mode == 0) {
      int blank = ((ins >> 4) & 7) + 1;
      y += blank;
      continue;
    }
    for (int r = 0; r < rows && y < a->frame_lines; r++, y++)
      draw_mode(a, mode, y, r, mem, width);
    mem = (uint16_t)(mem + line_bytes(mode, width));
  }
  for (int line = 0; line < a->frame_lines; line++)
    draw_pmg(a, line);
}
