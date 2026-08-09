#ifndef A800_ATARI_H
#define A800_ATARI_H

#include <stddef.h>
#include <stdint.h>

typedef struct Atari Atari;

enum { A800_WIDTH = 384, A800_MAX_HEIGHT = 312, A800_XEX_BOOT_FRAMES = 85 };
enum { A800_AUDIO_EVENTS = 2048 };
typedef enum { A800_NTSC = 0, A800_PAL = 1 } AtariVideoStandard;
typedef struct {
  uint64_t cycle;
  uint8_t reg, value;
} AtariAudioEvent;

struct Atari {
  uint8_t ram[65536];
  uint8_t os[16384];
  uint8_t basic[8192];
  uint8_t antic[16], gtia[32], pokey[16], pia[4];
  uint8_t gtia_collisions[16];
  uint8_t pia_ddr[2];
  uint8_t pokey_irqen, pokey_irqst, pokey_skctl, pokey_skstat;
  uint32_t pixels[A800_WIDTH * A800_MAX_HEIGHT];
  uint8_t line_colors[A800_MAX_HEIGHT][5];
  uint8_t line_gtia[A800_MAX_HEIGHT][32];
  uint8_t line_antic[A800_MAX_HEIGHT][16];
  uint8_t line_pmg[A800_MAX_HEIGHT][5], pm_latch[5];
  uint16_t collision_map[A800_WIDTH * A800_MAX_HEIGHT];
  uint16_t display_list, screen_memory;
  uint8_t display_mode, display_row, display_rows, display_width,
      display_scroll, display_dli, display_vscroll_active, display_first_line,
      display_stopped;
  uint8_t display_dma_bytes;
  uint16_t pc;
  uint8_t a, x, y, s, p;
  uint64_t cycles;
  int scanline, line_cycles, cpu_cycle_debt;
  AtariVideoStandard video_standard;
  int frame_lines;
  unsigned cpu_hz;
  double frame_rate;
  int irq, nmi, stopped;
  uint8_t keycode, console, joystick, trigger[4], trigger_latch[4];
  unsigned option_override;
  uint32_t rng, pokey_random_poly9;
  uint64_t pokey_random_cycle;
  double audio_phase[4];
  double audio_poly_phase, audio_previous, audio_filter;
  uint32_t audio_poly4, audio_poly5, audio_poly9, audio_poly17;
  uint8_t audio_output[4], audio_regs[9], audio_hp[2], audio_skctl,
      audio_started;
  AtariAudioEvent audio_events[A800_AUDIO_EVENTS];
  unsigned audio_event_read, audio_event_write;
  double audio_render_cycle;
  uint64_t pokey_timer_base, pokey_timer_count[4];
  uint8_t *disk;
  size_t disk_size;
  int disk_readonly;
  uint8_t *xex_data;
  size_t xex_size, xex_pos;
  uint16_t xex_first;
};

int Atari_Init(Atari *atari, const char *os_path, const char *basic_path);
void Atari_SetVideoStandard(Atari *atari, AtariVideoStandard standard);
void Atari_Reset(Atari *atari);
void Atari_Frame(Atari *atari);
void Atari_Destroy(Atari *atari);
uint8_t Atari_PortB(const Atari *atari);
uint8_t Atari_Read(Atari *atari, uint16_t address);
void Atari_Write(Atari *atari, uint16_t address, uint8_t value);
int Atari_LoadDisk(Atari *atari, const char *path);
int Atari_LoadExecutable(Atari *atari, const char *path);
int Atari_Sector(Atari *atari, unsigned sector, uint8_t *data, int write);

#endif
