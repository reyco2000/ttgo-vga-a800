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
static int line_bytes(int mode, int width) {
  if (mode == 6 || mode == 7)
    return width == 3 ? 24 : width == 2 ? 20 : 16;
  return width == 3 ? 48 : width == 2 ? 40 : 32;
}
static void draw_mode(Atari *a, int mode, int y, int row, uint16_t mem,
                      int width) {
  int bytes = line_bytes(mode, width), scale = (mode == 6 || mode == 7) ? 2 : 1,
      x = (A800_WIDTH - bytes * 8 * scale) / 2;
  uint8_t bg = a->gtia[26],
          pf[4] = {a->gtia[22], a->gtia[23], a->gtia[24], a->gtia[25]};
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
        span(a, y, x + b * 8 + k, 1, (bits & (128 >> k)) ? ink : paper);
    } else if (mode == 4 || mode == 5) {
      int glyph_row = mode == 5 ? row / 2 : row;
      uint16_t ch = (uint16_t)(((a->antic[9] & 0xfc) << 8) |
                               ((d & 127) * 8) + (glyph_row & 7));
      uint8_t bits = Atari_Read(a, ch);
      for (int k = 0; k < 4; k++) {
        unsigned v = (bits >> (6 - k * 2)) & 3;
        uint8_t color = v ? (v == 3 && d & 128 ? pf[3] : pf[v - 1]) : bg;
        span(a, y, x + b * 8 + k * 2, 2, color);
      }
    } else if (mode == 6 || mode == 7) {
      int glyph_row = mode == 7 ? row / 2 : row;
      uint16_t ch = (uint16_t)(((a->antic[9] & 0xfe) << 8) |
                               ((d & 63) * 8) + (glyph_row & 7));
      uint8_t bits = Atari_Read(a, ch);
      uint8_t ink = pf[d >> 6];
      for (int k = 0; k < 8; k++)
        span(a, y, x + (b * 8 + k) * scale, scale,
             (bits & (128 >> k)) ? ink : bg);
    } else if (mode == 8 || mode == 10 || mode == 13 || mode == 14) {
      for (int k = 0; k < 4; k++)
        span(a, y, x + (b * 8 + k * 2) * scale, 2 * scale,
             pf[(d >> (6 - k * 2)) & 3]);
    } else {
      for (int k = 0; k < 8; k++)
        span(a, y, x + (b * 8 + k) * scale, scale,
             (d & (128 >> k)) ? pf[2] : bg);
    }
  }
}
void Antic_Frame(Atari *a) {
  uint16_t dl = (uint16_t)(a->antic[2] | a->antic[3] << 8), mem = 0;
  int y = 8, guard = 0;
  for (int i = 0; i < A800_WIDTH * a->frame_lines; i++)
    a->pixels[i] = Gtia_Color(a->gtia[26]);
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
    if (ins & 64) {
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
}
