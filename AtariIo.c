#include "AtariIo.h"
#include "6502.h"
#include "Pokey.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int AtariIo_LoadFile(const char *path, uint8_t *dst, size_t expected) {
#ifdef WASMCART
  extern int wc_load_asset(const char *, unsigned, void *, unsigned)
      __attribute__((import_module("env"), import_name("wc_load_asset")));
  int n = wc_load_asset(path, (unsigned)strlen(path), dst, (unsigned)expected);
  return n == (int)expected ? 0 : -1;
#else
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  size_t n = fread(dst, 1, expected, f);
  int extra = fgetc(f);
  fclose(f);
  return n == expected && extra == EOF ? 0 : -1;
#endif
}
int AtariIo_LoadDisk(Atari *a, const char *path) {
  FILE *f = fopen(path, "rb");
  long n;
  if (!f)
    return -1;
  if (fseek(f, 0, SEEK_END) || (n = ftell(f)) < 16 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return -1;
  }
  uint8_t *p = (uint8_t *)malloc((size_t)n);
  if (!p) {
    fclose(f);
    return -1;
  }
  if (fread(p, 1, (size_t)n, f) != (size_t)n || p[0] != 0x96 || p[1] != 0x02) {
    free(p);
    fclose(f);
    return -1;
  }
  fclose(f);
  free(a->disk);
  a->disk = p;
  a->disk_size = (size_t)n;
  a->disk_readonly = 1;
  return 0;
}
int AtariIo_Sector(Atari *a, unsigned sector, uint8_t *data, int write) {
  if (!a->disk || !sector)
    return -1;
  unsigned size = a->disk[4] | a->disk[5] << 8;
  if (!size)
    size = 128;
  size_t off = 16 + (sector - 1u) * 128u;
  unsigned len = 128;
  if (size > 128 && sector > 3) {
    off = 16 + 3 * 128 + (sector - 4u) * size;
    len = size;
  }
  if (off + len > a->disk_size)
    return -1;
  if (write) {
    if (a->disk_readonly)
      return -1;
    memcpy(a->disk + off, data, len);
  } else
    memcpy(data, a->disk + off, len);
  return (int)len;
}
uint8_t AtariIo_Sio(Atari *a) {
  uint8_t command = a->ram[0x302];
  uint16_t buffer = (uint16_t)(a->ram[0x304] | a->ram[0x305] << 8);
  unsigned sector = (unsigned)(a->ram[0x30a] | a->ram[0x30b] << 8);
  uint8_t data[256];
  int n = -1;
  if (a->ram[0x300] == 0x31 && a->ram[0x301] == 1) {
    if (command == 0x52) {
      n = AtariIo_Sector(a, sector, data, 0);
      if (n > 0)
        for (int i = 0; i < n; i++)
          Atari_Write(a, (uint16_t)(buffer + i), data[i]);
    } else if (command == 0x53 && a->disk) {
      data[0] = 0x10;
      data[1] = 0xff;
      data[2] = 0xe0;
      data[3] = 0;
      for (int i = 0; i < 4; i++)
        Atari_Write(a, (uint16_t)(buffer + i), data[i]);
      n = 4;
    }
  }
  a->ram[0x303] = (uint8_t)(n > 0 ? 1 : 0x8a);
  return a->ram[0x303];
}
int Atari_LoadDisk(Atari *a, const char *path) {
  return AtariIo_LoadDisk(a, path);
}
int Atari_Sector(Atari *a, unsigned s, uint8_t *d, int w) {
  return AtariIo_Sector(a, s, d, w);
}
static void open_editor(Atari *a) {
  /* A normal XL boot opens E: before trying the boot device. Direct XEX
   * loading bypasses that step, but programs may still use its OS state. */
  if (a->ram[0x340] != 0xff)
    return;
  a->ram[0x700] = 'E';
  a->ram[0x701] = ':';
  a->ram[0x702] = 0x9b;
  memset(a->ram + 0x340, 0, 16);
  a->ram[0x340] = 0xff;
  a->ram[0x342] = 3; /* CIO OPEN */
  a->ram[0x344] = 0;
  a->ram[0x345] = 7;
  a->ram[0x34a] = 0x0c;
  a->x = 0;
  a->s = 0xff;
  a->p = 0x24;
  a->irq = a->nmi = 0;
  a->ram[0x1ff] = 0xd5;
  a->ram[0x1fe] = 0xfd;
  a->s = 0xfd;
  a->pc = 0xe456;
  for (int limit = 200000; a->pc != 0xd5fe && limit > 0; limit--) {
    int cycles = Cpu_Step(a);
    Pokey_Advance(a, cycles);
    a->cycles += (unsigned)cycles;
  }
}
int Atari_LoadExecutableData(Atari *a, const uint8_t *data, size_t size) {
  if (!data || size < 6)
    return -1;
  open_editor(a);
  free(a->xex_data);
  a->xex_data = (uint8_t *)malloc(size);
  if (!a->xex_data)
    return -1;
  memcpy(a->xex_data, data, size);
  a->xex_size = size;
  a->xex_pos = 0;
  a->xex_first = 0xffff;
  a->ram[9] = 1;
  a->ram[0x244] = 0;
  a->ram[0x2f4] = a->antic[9] = 0xe0;
  a->ram[0x2e0] = a->ram[0x2e1] = a->ram[0x2e2] = a->ram[0x2e3] = 0;
  a->pia_ddr[1] = 0xff;
  a->pia[1] |= 0x83;
  a->pia[3] |= 4;
  a->console |= 4;
  a->option_override = 64;
  a->s = 0xff;
  a->p = 0x20;
  a->irq = a->nmi = 0;
  AtariIo_XexContinue(a);
  return a->xex_data || a->pc ? 0 : -1;
}
int Atari_LoadExecutable(Atari *a, const char *path) {
  FILE *f = fopen(path, "rb");
  long size;
  if (!f)
    return -1;
  if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 6 || fseek(f, 0, SEEK_SET)) {
    fclose(f);
    return -1;
  }
  uint8_t *data = (uint8_t *)malloc((size_t)size);
  if (!data) {
    fclose(f);
    return -1;
  }
  int result = -1;
  if (fread(data, 1, (size_t)size, f) == (size_t)size)
    result = Atari_LoadExecutableData(a, data, (size_t)size);
  free(data);
  fclose(f);
  return result;
}
void AtariIo_XexContinue(Atari *a) {
  while (a->xex_data) {
    while (a->xex_pos + 1 < a->xex_size && a->xex_data[a->xex_pos] == 0xff &&
           a->xex_data[a->xex_pos + 1] == 0xff)
      a->xex_pos += 2;
    if (a->xex_pos + 4 > a->xex_size)
      break;
    uint16_t start = (uint16_t)(a->xex_data[a->xex_pos] |
                                a->xex_data[a->xex_pos + 1] << 8),
             end = (uint16_t)(a->xex_data[a->xex_pos + 2] |
                              a->xex_data[a->xex_pos + 3] << 8);
    size_t count;
    a->xex_pos += 4;
    if (end < start ||
        (count = (size_t)end - start + 1) > a->xex_size - a->xex_pos) {
      a->xex_pos = a->xex_size;
      break;
    }
    if (a->xex_first == 0xffff)
      a->xex_first = start;
    memcpy(a->ram + start, a->xex_data + a->xex_pos, count);
    a->xex_pos += count;
    uint16_t init = (uint16_t)(a->ram[0x2e2] | a->ram[0x2e3] << 8);
    if (init) {
      a->ram[0x2e2] = a->ram[0x2e3] = 0;
      uint16_t ret = 0xd5fe;
      a->ram[0x100 | a->s--] = (uint8_t)(ret >> 8);
      a->ram[0x100 | a->s--] = (uint8_t)ret;
      a->pc = init;
      return;
    }
  }
  uint16_t run = (uint16_t)(a->ram[0x2e0] | a->ram[0x2e1] << 8);
  if (!run)
    run = a->xex_first;
  free(a->xex_data);
  a->xex_data = NULL;
  a->xex_size = a->xex_pos = 0;
  a->pc = run;
}
