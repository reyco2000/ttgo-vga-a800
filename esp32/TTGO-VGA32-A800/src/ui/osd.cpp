/* OSD drawing: pixels go straight into the scanned-out framebuffer rows.
 * Clipped to the 336x240 screen. */
#include "fabgl.h"
#include "osd.h"
#include "../hal/hal.h"
#include "../../config.h"

/* FabGL stores each 32-bit word's pixels in the order 2, 3, 0, 1. */
#define PIXEL(row, x) (row)[(x) ^ 2]

static const fabgl::FontInfo *const fonts[] = {
    &fabgl::FONT_8x14, &fabgl::FONT_SANSERIF_8x16, &fabgl::FONT_6x8};

void Osd_Begin(void) {}

static inline void put(int x, int y, uint8_t raw) {
  if ((unsigned)x < (unsigned)VIDEO_WIDTH && (unsigned)y < (unsigned)VIDEO_HEIGHT)
    PIXEL(Video_Row(y), x) = raw;
}

void Osd_Rect(int x, int y, int w, int h, uint8_t colour) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > VIDEO_WIDTH) w = VIDEO_WIDTH - x;
  if (y + h > VIDEO_HEIGHT) h = VIDEO_HEIGHT - y;
  uint8_t c = Video_Rgb(colour);
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

/* The title font is drawn twice, one pixel apart, for a bold face. */
int Osd_FontWidth(OsdFont font) {
  return fonts[font]->width + (font == OSD_FONT_TITLE);
}

int Osd_FontHeight(OsdFont font) { return fonts[font]->height; }

int Osd_TextWidth(const char *s, OsdFont font) {
  return (int)strlen(s) * Osd_FontWidth(font);
}

int Osd_Text(int x, int y, const char *s, uint8_t fg, uint8_t bg, OsdFont font) {
  const fabgl::FontInfo *f = fonts[font];
  int h = f->height, advance = Osd_FontWidth(font);
  bool bold = font == OSD_FONT_TITLE;
  uint8_t ink = Video_Rgb(fg), paper = bg == OSD_CLEAR ? 0 : Video_Rgb(bg);
  for (; *s; s++, x += advance) {
    unsigned c = (uint8_t)*s; /* code page 437: 0x10..0x1b are arrows */
    const uint8_t *glyph = f->data + c * h; /* widths up to 8: a byte a row */
    for (int j = 0; j < h; j++) {
      if ((unsigned)(y + j) >= (unsigned)VIDEO_HEIGHT)
        continue;
      uint8_t *row = Video_Row(y + j);
      unsigned bits = glyph[j] << 1; /* column i is bit 0x100 >> i */
      if (bold)
        bits |= bits >> 1;
      for (int i = 0; i < advance; i++) {
        if ((unsigned)(x + i) >= (unsigned)VIDEO_WIDTH)
          continue;
        if (bits & (0x100 >> i))
          PIXEL(row, x + i) = ink;
        else if (bg != OSD_CLEAR)
          PIXEL(row, x + i) = paper;
      }
    }
  }
  return x;
}

/* --- icons --------------------------------------------------------------- */

/* Pixel art, one character per pixel; '.' is transparent. */
static uint8_t art_colour(char c) {
  switch (c) {
  case 'k': return OSD_RGB(0, 0, 0);
  case 'd': return OSD_RGB(1, 1, 1);
  case 'g': return OSD_RGB(2, 2, 2);
  case 'w': return OSD_RGB(3, 3, 3);
  case 'b': return OSD_RGB(0, 1, 3);
  case 'B': return OSD_RGB(1, 2, 3);
  case 'r': return OSD_RGB(3, 0, 0);
  case 'o': return OSD_RGB(3, 2, 0);
  case 'y': return OSD_RGB(3, 3, 0);
  case 'G': return OSD_RGB(0, 3, 0);
  default: return OSD_CLEAR;
  }
}

static const char *const art_disk[16] = {
    "kkkkkkkkkkkkkk..", "kbbggggggggbbbk.", "kbbggggddggbbbbk",
    "kbbggggddggbbbbk", "kbbggggddggbbbbk", "kbbggggggggbbbbk",
    "kbbbbbbbbbbbbbbk", "kbbbbbbbbbbbbbbk", "kbbwwwwwwwwwwbbk",
    "kbbwrrrrrrrrwbbk", "kbbwwwwwwwwwwbbk", "kbbwkkkkkkkkwbbk",
    "kbbwwwwwwwwwwbbk", "kbbwkkkkkkkwwbbk", "kbbwwwwwwwwwwbbk",
    "kkkkkkkkkkkkkkkk"};

static const char *const art_cart[16] = {
    "..kkkkkkkkkkkk..", ".kddddddddddddk.", ".kdrrrrrrrrrrdk.",
    ".kdoooooooooodk.", ".kdyyyyyyyyyydk.", ".kdwwwwwwwwwwdk.",
    ".kdwkkkwkkkkwdk.", ".kdwwwwwwwwwwdk.", ".kddddddddddddk.",
    ".kddddddddddddk.", ".kkkkkkkkkkkkkk.", "..kddddddddddk..",
    "..kdydydydydyk..", "..kdydydydydyk..", "..kdydydydydyk..",
    "..kkkkkkkkkkkk.."};

static const char *const art_reset[16] = {
    "................", ".....oooooo.....", "...oooooooooo...",
    "..oooo....oooo..", ".ooo........ooo.", ".ooo........ooo.",
    "ooo.......oooooo", "ooo........oooo.", "ooo.........oo..",
    "ooo.............", ".ooo............", ".ooo........ooo.",
    "..oooo....oooo..", "...oooooooooo...", ".....oooooo.....",
    "................"};

static const char *const art_setup[16] = {
    "...kkkk.........", "...krrk.........", "gggkrrkggggggggg",
    "...krrk.........", "...kkkk.........", "..........kkkk..",
    "..........kyyk..", "ggggggggggkyykgg", "..........kyyk..",
    "..........kkkk..", "......kkkk......", "......kGGk......",
    "ggggggkGGkgggggg", "......kGGk......", "......kkkk......",
    "................"};

static const char *const art_about[16] = {
    ".....kkkkkk.....", "...kkbbbbbbkk...", "..kbbbbbbbbbbk..",
    ".kbbbbbwwbbbbbk.", ".kbbbbbwwbbbbbk.", "kbbbbbbbbbbbbbbk",
    "kbbbbbwwwbbbbbbk", "kbbbbbbwwbbbbbbk", "kbbbbbbwwbbbbbbk",
    "kbbbbbbwwbbbbbbk", "kbbbbbbwwbbbbbbk", ".kbbbbwwwwbbbbk.",
    ".kbbbbbbbbbbbbk.", "..kbbbbbbbbbbk..", "...kkbbbbbbkk...",
    ".....kkkkkk....."};

static const char *const art_resume[16] = {
    ".....kkkkkk.....", "...kkGGGGGGkk...", "..kGGGGGGGGGGk..",
    ".kGGGGwGGGGGGGk.", ".kGGGGwwGGGGGGk.", "kGGGGGwwwGGGGGGk",
    "kGGGGGwwwwGGGGGk", "kGGGGGwwwwwGGGGk", "kGGGGGwwwwwGGGGk",
    "kGGGGGwwwwGGGGGk", "kGGGGGwwwGGGGGGk", ".kGGGGwwGGGGGGk.",
    ".kGGGGwGGGGGGGk.", "..kGGGGGGGGGGk..", "...kkGGGGGGkk...",
    ".....kkkkkk....."};

static const char *const art_folder[8] = {
    "........", ".ooo....", "oyyyoooo", "oyyyyyyo",
    "oyyyyyyo", "oyyyyyyo", "oooooooo", "........"};

static const char *const art_atr[8] = {
    "BBBBBBB.", "BBgggBBB", "BBgdgBBB", "BBBBBBBB",
    "BwwwwwwB", "BwrrrrwB", "BwwwwwwB", "BBBBBBBB"};

static const char *const art_xex[8] = {
    ".G......", ".GGG....", ".GGGGG..", ".GGGGGGG",
    ".GGGGGGG", ".GGGGG..", ".GGG....", ".G......"};

static const char *const art_rom[8] = {
    "oooooooo", "owwwwwwo", "orrrrrro", "oyyyyyyo",
    "oooooooo", ".gggggg.", ".yyyyyy.", ".y.yy.y."};

static const char *const art_eject[8] = {
    "...rr...", "..rrrr..", ".rrrrrr.", "rrrrrrrr",
    "........", "rrrrrrrr", "rrrrrrrr", "........"};

static const char *const *const art[] = {
    art_disk, art_cart, art_reset, art_setup, art_about, art_resume,
    art_folder, art_atr, art_xex, art_rom, art_eject};

int Osd_IconSize(OsdIcon icon) { return icon < ICON_FOLDER ? 16 : 8; }

void Osd_Icon(int x, int y, OsdIcon icon, int scale) {
  const char *const *rows = art[icon];
  int n = Osd_IconSize(icon);
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++) {
      uint8_t c = art_colour(rows[j][i]);
      if (c != OSD_CLEAR)
        Osd_Rect(x + i * scale, y + j * scale, scale, scale, c);
    }
}
