#include "Atari.h"
#include "6502.h"
#include "Antic.h"
#include "AtariIo.h"
#include "Gtia.h"
#include "Pia.h"
#include "Pokey.h"
#include <stdlib.h>
#include <string.h>

int Atari_Init(Atari *a, const char *os, const char *basic) {
  memset(a, 0, sizeof(*a));
  a->console = 7;
  a->joystick = 0xff;
  memset(a->trigger, 1, sizeof(a->trigger));
  memset(a->trigger_latch, 1, sizeof(a->trigger_latch));
  a->rng = 0x19770825;
  Atari_SetVideoStandard(a, A800_NTSC);
  Pia_Reset(a);
  a->pokey_irqst = a->pokey_skstat = 0xff;
  if (AtariIo_LoadFile(os, a->os, sizeof(a->os)))
    return -1;
  if (basic && AtariIo_LoadFile(basic, a->basic, sizeof(a->basic)))
    return -2;
  Atari_Reset(a);
  return 0;
}
void Atari_SetVideoStandard(Atari *a, AtariVideoStandard standard) {
  a->video_standard = standard;
  if (standard == A800_PAL) {
    a->frame_lines = 312;
    a->cpu_hz = 1773447u;
    a->frame_rate = 49.8607597;
  } else {
    a->frame_lines = 262;
    a->cpu_hz = 1789772u;
    a->frame_rate = 59.9227434;
  }
}
void Atari_Reset(Atari *a) {
  memset(a->antic, 0, sizeof(a->antic));
  memset(a->gtia, 0, sizeof(a->gtia));
  memset(a->gtia_collisions, 0, sizeof(a->gtia_collisions));
  memset(a->pm_latch, 0, sizeof(a->pm_latch));
  memset(a->trigger_latch, 1, sizeof(a->trigger_latch));
  memset(a->pokey, 0, sizeof(a->pokey));
  a->pokey_irqen = a->pokey_skctl = 0;
  a->pokey_irqst = a->pokey_skstat = 0xff;
  Pia_Reset(a);
  a->cycles = 0;
  a->irq = a->nmi = 0;
  Cpu_Reset(a);
}
uint8_t Atari_Read(Atari *a, uint16_t p) {
  uint8_t portb = Atari_PortB(a);
  if (p >= 0xd000 && p < 0xd800) {
    switch ((p >> 8) & 7) {
    case 0:
      return Gtia_Read(a, p);
    case 2:
      return Pokey_Read(a, p);
    case 3:
      return Pia_Read(a, p);
    case 4:
      return Antic_Read(a, p);
    default:
      return 0xff;
    }
  }
  if (p >= 0xc000 && (portb & 1))
    return a->os[p - 0xc000];
  if (p >= 0xa000 && p < 0xc000 && !(portb & 2))
    return a->basic[p - 0xa000];
  if (p >= 0x5000 && p < 0x5800 && (portb & 1) && !(portb & 0x80))
    return a->os[0x1000 + p - 0x5000];
  return a->ram[p];
}
void Atari_Write(Atari *a, uint16_t p, uint8_t v) {
  if (p >= 0xd000 && p < 0xd800) {
    switch ((p >> 8) & 7) {
    case 0:
      Gtia_Write(a, p, v);
      break;
    case 2:
      Pokey_Write(a, p, v);
      break;
    case 3:
      Pia_Write(a, p, v);
      break;
    case 4:
      Antic_Write(a, p, v);
      break;
    }
    return;
  }
  uint8_t pb = Atari_PortB(a);
  if ((p >= 0xc000 && (pb & 1)) || (p >= 0xa000 && p < 0xc000 && !(pb & 2)))
    return;
  a->ram[p] = v;
}
void Atari_Frame(Atari *a) {
  Antic_ScheduleDlis(a);
  Antic_BeginFrame(a);
  for (a->scanline = 0; a->scanline < a->frame_lines; a->scanline++) {
    a->line_cycles = 0;
    if (a->dli_lines[a->scanline]) {
      a->antic[15] = (uint8_t)((a->antic[15] & ~0x40) | 0x80);
      if (a->antic[14] & 0x80)
        a->nmi = 1;
    }
    if (a->scanline == 248) {
      a->antic[15] = (uint8_t)((a->antic[15] & ~0x80) | 0x40);
      if (a->antic[14] & 0x40)
        a->nmi = 1;
    }
    while (a->line_cycles < 114) {
      int n = Cpu_Step(a);
      a->line_cycles += n;
      Pokey_Advance(a, n);
    }
    Antic_CaptureLine(a);
    memcpy(a->line_gtia[a->scanline], a->gtia, sizeof(a->gtia));
    for (int i = 0; i < 4; i++)
      a->line_colors[a->scanline][i] = a->gtia[22 + i];
    a->line_colors[a->scanline][4] = a->gtia[26];
    Antic_RenderLine(a, a->scanline);
  }
  Gtia_Frame(a);
}
void Atari_Destroy(Atari *a) {
  free(a->disk);
  free(a->xex_data);
  a->disk = a->xex_data = NULL;
  a->disk_size = a->xex_size = a->xex_pos = 0;
}
