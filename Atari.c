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
  a->rng = a->pokey_random_poly9 = 1;
#ifdef NO_NTSC
  Atari_SetVideoStandard(a, A800_PAL);
#else
  Atari_SetVideoStandard(a, A800_NTSC);
#endif
  Pia_Reset(a);
  a->pokey_irqst = a->pokey_skstat = 0xff;
  if (AtariIo_LoadOs(a, os))
    return -1;
#ifndef NO_BASIC
  if (basic) {
    if (a->machine_type != A800_MACHINE_XL)
      return -3;
    if (AtariIo_LoadFile(basic, a->basic, sizeof(a->basic)))
      return -2;
  }
#else
  (void)basic;
#endif
  Atari_Reset(a);
  return 0;
}
void Atari_SetVideoStandard(Atari *a, AtariVideoStandard standard) {
#ifdef NO_NTSC
  standard = A800_PAL;
#elif defined(NO_PAL)
  standard = A800_NTSC;
#endif
  a->video_standard = standard;
  a->palette = Gtia_Palette(standard);
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
const char *Atari_MachineName(const Atari *a) {
  if (a->machine_type == A800_MACHINE_800)
    return "400/800";
  if (a->machine_type == A800_MACHINE_5200)
    return "5200";
  return "XL/XE";
}
void Atari_Reset(Atari *a) {
  memset(a->antic, 0, sizeof(a->antic));
  memset(a->gtia, 0, sizeof(a->gtia));
  memset(a->gtia_collisions, 0, sizeof(a->gtia_collisions));
  memset(a->pm_latch, 0, sizeof(a->pm_latch));
  memset(a->trigger_latch, 1, sizeof(a->trigger_latch));
  memset(a->pokey, 0, sizeof(a->pokey));
  memset(a->pokey_timer_count, 0, sizeof(a->pokey_timer_count));
  memset(a->audio_phase, 0, sizeof(a->audio_phase));
  memset(a->audio_output, 0, sizeof(a->audio_output));
  memset(a->audio_hp, 0, sizeof(a->audio_hp));
  a->audio_poly_phase = a->audio_previous = a->audio_filter = a->audio_sum = 0;
  a->audio_poly4 = a->audio_poly5 = a->audio_poly9 = a->audio_poly17 = 1;
  a->audio_rate = a->audio_len = a->audio_clocks = 0;
  a->pokey_timer_base = a->pokey_random_cycle = 0;
  a->rng = a->pokey_random_poly9 = 1;
  a->pokey_irqen = a->pokey_skctl = 0;
  a->pokey_irqst = a->pokey_skstat = 0xff;
  Pia_Reset(a);
  a->cycles = 0;
  a->cpu_cycle_debt = a->irq = a->nmi = 0;
  Cpu_Reset(a);
}
uint8_t Atari_Read(Atari *a, uint16_t p) {
  uint8_t portb = Atari_PortB(a);
  if (a->machine_type == A800_MACHINE_5200) {
    if (p >= 0xc000 && p < 0xd000)
      return Gtia_Read(a, p);
    if (p >= 0xd400 && p < 0xd500)
      return Antic_Read(a, p);
    if (p >= 0xe800 && p < 0xf000)
      return Pokey_Read(a, p);
  } else if (p >= 0xd000 && p < 0xd800) {
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
  if (p >= 0xc000 && (a->os_pages[p >> 11] & (1u << ((p >> 8) & 7))) &&
      (a->machine_type != A800_MACHINE_XL || (portb & 1)))
    return a->os[p - 0xc000];
#ifndef NO_BASIC
  if (a->machine_type == A800_MACHINE_XL && p >= 0xa000 && p < 0xc000 &&
      !(portb & 2))
    return a->basic[p - 0xa000];
#else
  if (a->machine_type == A800_MACHINE_XL && p >= 0xa000 && p < 0xc000 &&
      !(portb & 2))
    return 0;
#endif
  if (a->machine_type == A800_MACHINE_XL && p >= 0x5000 && p < 0x5800 &&
      (a->os_pages[p >> 11] & (1u << ((p >> 8) & 7))) && (portb & 1) &&
      !(portb & 0x80))
    return a->os[0x1000 + p - 0x5000];
  return a->ram[p];
}
void Atari_Write(Atari *a, uint16_t p, uint8_t v) {
  if (a->machine_type == A800_MACHINE_5200) {
    if (p >= 0xc000 && p < 0xd000) {
      Gtia_Write(a, p, v);
      return;
    }
    if (p >= 0xd400 && p < 0xd500) {
      Antic_Write(a, p, v);
      return;
    }
    if (p >= 0xe800 && p < 0xf000) {
      Pokey_Write(a, p, v);
      return;
    }
  } else if (p >= 0xd000 && p < 0xd800) {
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
  if (p >= 0xc000 && (a->os_pages[p >> 11] & (1u << ((p >> 8) & 7))) &&
      (a->machine_type != A800_MACHINE_XL || (pb & 1)))
    return;
  if (a->machine_type == A800_MACHINE_XL && p >= 0xa000 && p < 0xc000 &&
      !(pb & 2))
    return;
  if (a->machine_type == A800_MACHINE_XL && p >= 0x5000 && p < 0x5800 &&
      (a->os_pages[p >> 11] & (1u << ((p >> 8) & 7))) && (pb & 1) &&
      !(pb & 0x80))
    return;
  a->ram[p] = v;
}
void Atari_Frame(Atari *a) {
  Antic_BeginFrame(a);
  for (a->scanline = 0; a->scanline < a->frame_lines; a->scanline++) {
    a->line_cycles = 0;
    uint64_t line_start = a->cycles;
    Antic_BeginLine(a, a->scanline);
    if (a->scanline == 248) {
      a->antic[15] = (uint8_t)((a->antic[15] & ~0x80) | 0x40);
      if (a->antic[14] & 0x40)
        a->nmi = 1;
    }
    int cpu_limit = 114 - Antic_DmaCycles(a) - a->cpu_cycle_debt;
    if (cpu_limit < 1)
      cpu_limit = 1;
    a->cpu_cycle_debt = 0;
    while (a->line_cycles < cpu_limit) {
      int n = Cpu_Step(a);
      a->line_cycles += n;
      Pokey_Advance(a, n);
    }
    unsigned cpu_used = (unsigned)(a->cycles - line_start);
    if (cpu_used > (unsigned)cpu_limit)
      a->cpu_cycle_debt = (int)(cpu_used - (unsigned)cpu_limit);
    unsigned elapsed = (unsigned)(a->cycles - line_start);
    if (elapsed < 114) {
      unsigned idle = 114 - elapsed;
      a->cycles += idle;
      Pokey_Advance(a, (int)idle);
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
