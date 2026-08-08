#include "AtariIo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int AtariIo_LoadFile(const char *path, uint8_t *dst, size_t expected) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  size_t n = fread(dst, 1, expected, f);
  int extra = fgetc(f);
  fclose(f);
  return n == expected && extra == EOF ? 0 : -1;
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
int Atari_LoadDisk(Atari *a, const char *path) {
  return AtariIo_LoadDisk(a, path);
}
int Atari_Sector(Atari *a, unsigned s, uint8_t *d, int w) {
  return AtariIo_Sector(a, s, d, w);
}
