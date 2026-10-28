/* On-screen display primitives for the supervisor menu: rectangles, text
 * in the Atari OS 8x8 font and small icons, drawn straight into the VGA
 * framebuffer. Colours are Atari palette indices. No allocation. */
#ifndef A800_ESP32_OSD_H
#define A800_ESP32_OSD_H
#include <stdint.h>
extern "C" {
#include "../core/Atari.h"
}

enum OsdIcon { ICON_DISK, ICON_CART, ICON_RESET, ICON_SETUP, ICON_ABOUT, ICON_RESUME };

enum {
  OSD_BG = 0x94,  /* Atari editor blue */
  OSD_TILE = 0x86,
  OSD_FG = 0x0f,
  OSD_SEL_BG = 0x0f, /* inverted selection */
  OSD_SEL_FG = 0x94
};

void Osd_Begin(const Atari *a); /* remembers the OS font ($E000) */
void Osd_Rect(int x, int y, int w, int h, uint8_t colour);
void Osd_Frame(int x, int y, int w, int h, uint8_t colour);
void Osd_Text(int x, int y, const char *s, uint8_t fg, uint8_t bg);
int Osd_TextCols(void); /* characters per screen row */
/* About 32x24; bg is the tile colour, used for the icon's cut-outs. */
void Osd_Icon(int x, int y, OsdIcon icon, uint8_t fg, uint8_t bg);

#endif
