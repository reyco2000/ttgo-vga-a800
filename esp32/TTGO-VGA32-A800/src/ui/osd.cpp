/* OSD drawing: pixels go straight into the scanned-out framebuffer rows.
 * Clipped to the 336x240 screen. */
#include "osd.h"
#include "../hal/hal.h"
#include "../../config.h"

/* FabGL stores each 32-bit word's pixels in the order 2, 3, 0, 1. */
#define PIXEL(row, x) (row)[(x) ^ 2]

static const uint8_t *font; /* 128 glyphs x 8 bytes, ANTIC internal order */

void Osd_Begin(const Atari *a) { font = a->os + 0x2000; }

static inline void put(int x, int y, uint8_t colour) {
  if ((unsigned)x < (unsigned)VIDEO_WIDTH && (unsigned)y < (unsigned)VIDEO_HEIGHT)
    PIXEL(Video_Row(y), x) = colour;
}

void Osd_Rect(int x, int y, int w, int h, uint8_t colour) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > VIDEO_WIDTH) w = VIDEO_WIDTH - x;
  if (y + h > VIDEO_HEIGHT) h = VIDEO_HEIGHT - y;
  uint8_t c = Video_Colour(colour);
  for (int j = 0; j < h; j++) {
    uint8_t *row = Video_Row(y + j);
    for (int i = 0; i < w; i++)
      PIXEL(row, x + i) = c;
  }
}

void Osd_Frame(int x, int y, int w, int h, uint8_t colour) {
  Osd_Rect(x, y, w, 1, colour);
  Osd_Rect(x, y + h - 1, w, 1, colour);
  Osd_Rect(x, y, 1, h, colour);
  Osd_Rect(x + w - 1, y, 1, h, colour);
}

int Osd_TextCols(void) { return VIDEO_WIDTH / 8; }

void Osd_Text(int x, int y, const char *s, uint8_t fg, uint8_t bg) {
  if (!font)
    return;
  uint8_t f = Video_Colour(fg), b = Video_Colour(bg);
  for (; *s; s++, x += 8) {
    unsigned c = (uint8_t)*s;
    if (c < 0x20 || c > 0x7f)
      c = '?';
    unsigned code = c < 0x60 ? c - 0x20 : c;
    const uint8_t *glyph = font + code * 8;
    for (int j = 0; j < 8; j++) {
      if ((unsigned)(y + j) >= (unsigned)VIDEO_HEIGHT)
        continue;
      uint8_t *row = Video_Row(y + j);
      for (int i = 0; i < 8; i++)
        if ((unsigned)(x + i) < (unsigned)VIDEO_WIDTH)
          PIXEL(row, x + i) = glyph[j] & (0x80 >> i) ? f : b;
    }
  }
}

static void line(int x0, int y0, int x1, int y1, uint8_t colour) {
  uint8_t c = Video_Colour(colour);
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    put(x0, y0, c);
    if (x0 == x1 && y0 == y1)
      break;
    int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}

/* Circle outline by midpoint steps. */
static void ring(int cx, int cy, int r, uint8_t colour) {
  uint8_t c = Video_Colour(colour);
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    put(cx + x, cy + y, c); put(cx - x, cy + y, c);
    put(cx + x, cy - y, c); put(cx - x, cy - y, c);
    put(cx + y, cy + x, c); put(cx - y, cy + x, c);
    put(cx + y, cy - x, c); put(cx - y, cy - x, c);
    y++;
    if (err < 0) err += 2 * y + 1;
    else x--, err += 2 * (y - x) + 1;
  }
}

void Osd_Icon(int x, int y, OsdIcon icon, uint8_t fg, uint8_t bg) {
  switch (icon) {
  case ICON_DISK: /* floppy: body, shutter, label */
    Osd_Frame(x + 4, y, 24, 24, fg);
    Osd_Rect(x + 9, y, 14, 8, fg);
    Osd_Rect(x + 17, y + 1, 3, 6, bg);
    Osd_Frame(x + 8, y + 12, 16, 12, fg);
    break;
  case ICON_CART: /* cartridge: body with notch and contacts */
    Osd_Frame(x + 4, y + 2, 24, 16, fg);
    Osd_Rect(x + 8, y + 6, 16, 3, fg);
    Osd_Rect(x + 8, y + 12, 16, 2, fg);
    Osd_Rect(x + 8, y + 18, 16, 5, fg);
    for (int i = 0; i < 5; i++)
      Osd_Rect(x + 9 + i * 3, y + 19, 1, 4, bg);
    break;
  case ICON_RESET: /* circular arrow */
    ring(x + 16, y + 12, 10, fg);
    ring(x + 16, y + 12, 9, fg);
    Osd_Rect(x + 16, y + 1, 12, 8, bg);       /* gap at upper right */
    Osd_Rect(x + 16, y + 3, 11, 2, fg);       /* arrow head */
    line(x + 27, y + 4, x + 22, y, fg);
    line(x + 27, y + 4, x + 22, y + 8, fg);
    break;
  case ICON_SETUP: /* three slider bars */
    for (int i = 0; i < 3; i++) {
      Osd_Rect(x + 4, y + 3 + i * 8, 24, 2, fg);
      Osd_Rect(x + 8 + ((i * 9) % 14), y + 1 + i * 8, 4, 6, fg);
    }
    break;
  case ICON_ABOUT: /* circled i */
    ring(x + 16, y + 12, 11, fg);
    ring(x + 16, y + 12, 10, fg);
    Osd_Rect(x + 15, y + 5, 3, 3, fg);
    Osd_Rect(x + 15, y + 10, 3, 9, fg);
    break;
  case ICON_RESUME: /* play triangle */
    for (int i = 0; i < 20; i++) {
      int half = (19 - i) / 2 + 1;
      Osd_Rect(x + 8 + i, y + 12 - half, 1, 2 * half, fg);
    }
    break;
  }
}
