#ifndef A800_ATARI_H
#define A800_ATARI_H

#include <stddef.h>
#include <stdint.h>

typedef struct Atari Atari;
typedef struct AnticLine AnticLine;

enum { A800_WIDTH = 384, A800_MAX_HEIGHT = 312, A800_XEX_BOOT_FRAMES = 85 };
/* A800_LINE_BUFFER keeps only the scanline being drawn; frontends receive
 * each finished line through line_sink instead of reading a full frame. */
#ifdef A800_LINE_BUFFER
enum { A800_PIXEL_ROWS = 1 };
#define A800_LINE(y) ((void)(y), 0)
#else
enum { A800_PIXEL_ROWS = A800_MAX_HEIGHT };
#define A800_LINE(y) (y)
#endif
/* Marks the externally called per-cycle and per-line functions; ports can
 * place them in fast memory, e.g. -DA800_HOT=IRAM_ATTR on the ESP32. Static
 * helpers stay unmarked: GCC will not inline across differing sections, so
 * marking them too would turn every inlined helper into a call. */
#ifndef A800_HOT
#define A800_HOT
#endif
/* For tiny helpers used from very large functions (Cpu_Step), where GCC's
 * growth limits would otherwise leave them as calls. */
#if defined(__GNUC__)
#define A800_INLINE static inline __attribute__((always_inline))
#else
#define A800_INLINE static inline
#endif
#ifndef A800_AUDIO_BUFFER
#define A800_AUDIO_BUFFER 8192
#endif
typedef enum { A800_NTSC = 0, A800_PAL = 1 } AtariVideoStandard;
typedef enum {
  A800_MACHINE_800 = 1,
  A800_MACHINE_XL = 2,
  A800_MACHINE_5200 = 3
} AtariMachineType;

struct Atari {
  uint8_t ram[65536];
  uint8_t os[16384];
  uint8_t os_pages[32];
#ifndef NO_BASIC
  uint8_t basic[8192];
#endif
  uint8_t antic[16], gtia[32], pokey[16], pia[4];
  uint8_t gtia_collisions[16];
  uint8_t pia_ddr[2];
  uint8_t pokey_irqen, pokey_irqst, pokey_skctl, pokey_skstat;
  uint8_t pixels[A800_WIDTH * A800_PIXEL_ROWS];
  const uint32_t *palette;
  uint8_t line_colors[A800_PIXEL_ROWS][5];
  uint8_t line_gtia[A800_PIXEL_ROWS][32];
  uint8_t line_antic[A800_PIXEL_ROWS][16];
  uint8_t line_pmg[A800_PIXEL_ROWS][5], pm_latch[5];
  uint16_t collision_map[A800_WIDTH * A800_PIXEL_ROWS];
  void (*line_sink)(void *context, int y, const uint8_t *line);
  /* Optional: lines without player/missile graphics go here instead of
   * being drawn, for the frontend to draw with Antic_DrawLine (possibly on
   * another core). Lines with them are still drawn and sent to line_sink. */
  void (*line_defer)(void *context, const AnticLine *line);
  void *line_sink_context;
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
  AtariMachineType machine_type;
  int frame_lines;
  unsigned cpu_hz;
  double frame_rate;
  int irq, nmi, stopped;
  uint8_t keycode, console, joystick, trigger[4], trigger_latch[4];
  unsigned option_override;
  uint32_t rng, pokey_random_poly9;
  uint64_t pokey_random_cycle;
  int32_t audio_phase[4];
  uint32_t audio_poly_phase, audio_sum, audio_pending;
  double audio_previous, audio_filter;
  uint32_t audio_poly4, audio_poly5, audio_poly9, audio_poly17;
  uint8_t audio_output[4], audio_hp[2];
  int audio_rate, audio_len, audio_clocks;
  int16_t audio_buffer[A800_AUDIO_BUFFER];
  uint64_t pokey_timer_base, pokey_timer_count[4];
  /* Per-page fast paths filled on first use: read_map[page] points at the
   * 256 bytes that page reads from, write_map[page] at the RAM it writes to.
   * NULL means unfilled, I/O or ROM; Atari_InvalidateMemoryMap() clears
   * both whenever PORTB, the OS image or the machine type changes. */
  const uint8_t *read_map[256];
  uint8_t *write_map[256];
  uint8_t *disk;
  size_t disk_size;
  int disk_readonly;
  uint8_t *xex_data;
  size_t xex_size, xex_pos;
  uint16_t xex_first;
};

int Atari_Init(Atari *atari, const char *os_path, const char *basic_path);
void Atari_SetVideoStandard(Atari *atari, AtariVideoStandard standard);
const char *Atari_MachineName(const Atari *atari);
void Atari_Reset(Atari *atari);
void Atari_Frame(Atari *atari);
void Atari_Destroy(Atari *atari);
uint8_t Atari_PortB(const Atari *atari);
uint8_t Atari_Read(Atari *atari, uint16_t address);
void Atari_Write(Atari *atari, uint16_t address, uint8_t value);
void Atari_InvalidateMemoryMap(Atari *atari);
static inline uint8_t Atari_ReadFast(Atari *atari, uint16_t address) {
  const uint8_t *page = atari->read_map[address >> 8];
  return page ? page[address & 255] : Atari_Read(atari, address);
}
static inline void Atari_WriteFast(Atari *atari, uint16_t address,
                                   uint8_t value) {
  uint8_t *page = atari->write_map[address >> 8];
  if (page)
    page[address & 255] = value;
  else
    Atari_Write(atari, address, value);
}
int Atari_LoadDisk(Atari *atari, const char *path);
int Atari_LoadExecutable(Atari *atari, const char *path);
int Atari_Sector(Atari *atari, unsigned sector, uint8_t *data, int write);

#endif
