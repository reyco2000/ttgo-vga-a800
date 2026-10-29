/* On-screen display primitives for the supervisor menu: rectangles, text
 * in FabGL's bitmap fonts and multicolour pixel-art icons, drawn straight
 * into the VGA framebuffer. Colours are RGB222 codes (OSD_RGB), not Atari
 * palette indices, so the menu gets the DAC's pure colours. No allocation. */
#ifndef A800_ESP32_OSD_H
#define A800_ESP32_OSD_H
#include <stdint.h>

#define OSD_RGB(r, g, b) ((r) | (g) << 2 | (b) << 4) /* 0..3 each */

enum OsdIcon {
  ICON_DISK, ICON_CART, ICON_RESET, ICON_SETUP, ICON_ABOUT, ICON_RESUME,
  /* 8x8 list markers */
  ICON_FOLDER, ICON_ATR, ICON_XEX, ICON_ROM, ICON_EJECT
};

enum OsdFont {
  OSD_FONT_BODY,  /* VGA 8x14 */
  OSD_FONT_TITLE, /* sans serif 8x16, bold */
  OSD_FONT_SMALL, /* 6x8 */
  OSD_FONT_TINY   /* 4x6, key cap legends */
};

enum {
  OSD_BG = OSD_RGB(0, 0, 1),       /* navy */
  OSD_PANEL = OSD_RGB(1, 1, 2),    /* tiles, popups */
  OSD_PANEL_HI = OSD_RGB(0, 1, 3), /* selected tile */
  OSD_HEADER = OSD_RGB(0, 1, 2),
  OSD_BORDER = OSD_RGB(1, 2, 3),
  OSD_SHADOW = OSD_RGB(0, 0, 0),
  OSD_FG = OSD_RGB(3, 3, 3),       /* plain text */
  OSD_DIM = OSD_RGB(2, 2, 3),      /* secondary text */
  OSD_TITLE = OSD_RGB(3, 3, 1),    /* screen titles */
  OSD_LABEL = OSD_RGB(1, 3, 3),    /* "name:" labels, folders */
  OSD_VALUE = OSD_RGB(3, 3, 0),    /* editable values */
  OSD_GOOD = OSD_RGB(1, 3, 1),     /* executables, OK */
  OSD_WARM = OSD_RGB(3, 2, 0),     /* cartridges */
  OSD_ALERT = OSD_RGB(3, 1, 1),    /* eject, warnings */
  OSD_SEL_BG = OSD_RGB(0, 2, 3),   /* list selection bar */
  OSD_SEL_FG = OSD_RGB(3, 3, 3),
  OSD_KEY_BG = OSD_RGB(2, 2, 2),   /* key caps in the hint line */
  OSD_KEY_FG = OSD_RGB(0, 0, 1),
  OSD_CLEAR = 0xff                 /* text background: leave pixels alone */
};

void Osd_Begin(void);
void Osd_Rect(int x, int y, int w, int h, uint8_t colour);
void Osd_Frame(int x, int y, int w, int h, uint8_t colour);
/* Returns the x after the last character. bg OSD_CLEAR draws only the ink. */
int Osd_Text(int x, int y, const char *s, uint8_t fg, uint8_t bg = OSD_CLEAR,
             OsdFont font = OSD_FONT_BODY);
int Osd_TextWidth(const char *s, OsdFont font = OSD_FONT_BODY);
int Osd_FontWidth(OsdFont font);  /* advance per character */
int Osd_FontHeight(OsdFont font);
/* Tile icons are 16x16, markers 8x8, both drawn scale times larger. */
void Osd_Icon(int x, int y, OsdIcon icon, int scale = 1);
int Osd_IconSize(OsdIcon icon);

#endif
