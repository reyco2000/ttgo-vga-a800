#ifndef A800_ATARI_H
#define A800_ATARI_H

#include <stddef.h>
#include <stdint.h>

typedef struct Atari Atari;

enum { A800_WIDTH = 384, A800_MAX_HEIGHT = 312 };
typedef enum { A800_NTSC = 0, A800_PAL = 1 } AtariVideoStandard;

struct Atari {
  uint8_t ram[65536];
  uint8_t os[16384];
  uint8_t basic[8192];
  uint8_t antic[16], gtia[32], pokey[16], pia[4];
  uint8_t pia_ddr[2];
  uint8_t pokey_irqen, pokey_irqst, pokey_skctl, pokey_skstat;
  uint32_t pixels[A800_WIDTH * A800_MAX_HEIGHT];
  uint16_t pc;
  uint8_t a, x, y, s, p;
  uint64_t cycles;
  int scanline, line_cycles;
  AtariVideoStandard video_standard;
  int frame_lines;
  unsigned cpu_hz;
  double frame_rate;
  int irq, nmi, stopped;
  uint8_t keycode, console, joystick, trigger;
  uint32_t rng;
  double audio_phase[4];
  uint8_t *disk;
  size_t disk_size;
  int disk_readonly;
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
int Atari_Sector(Atari *atari, unsigned sector, uint8_t *data, int write);

#endif
