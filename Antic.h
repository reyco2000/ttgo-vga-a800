#ifndef A800_ANTIC_H
#define A800_ANTIC_H
#include "Atari.h"
uint8_t Antic_Read(Atari *a, uint16_t address);
void Antic_Write(Atari *a, uint16_t address, uint8_t value);
void Antic_Frame(Atari *a);
void Antic_BeginFrame(Atari *a);
void Antic_BeginLine(Atari *a, int line);
int Antic_DmaCycles(const Atari *a);
void Antic_RenderLine(Atari *a, int line);
void Antic_CaptureLine(Atari *a);
/* Everything needed to draw one scanline, captured when ANTIC reaches it:
 * the line's register snapshot and the playfield bytes it fetched. A line
 * can then be drawn later, or on another core, with no access to the
 * machine. */
struct AnticLine {
  int16_t y;
  uint8_t mode, row, width, scroll, pmg_area;
  uint8_t colors[5], gtia[32], antic[16], pmg[5];
  uint8_t data[48], glyph[48];
};
/* Advances the display state for line y and fills *line. */
void Antic_PrepareLine(Atari *a, int y, AnticLine *line);
/* Draws a prepared line into A800_WIDTH pixels. With hits and collisions,
 * also draws players and missiles and accumulates collisions; lines for
 * which Antic_LineHasPmg() is false need neither. */
void Antic_DrawLine(const AnticLine *line, uint8_t *pixels, uint16_t *hits,
                    uint8_t *collisions);
int Antic_LineHasPmg(const AnticLine *line);
#endif
