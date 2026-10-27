#include "State.h"
#include "Gtia.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char magic[8];
  uint32_t version, atari_size, features, standard;
  uint64_t disk_size, xex_size;
} StateHeader;

enum {
  STATE_VERSION = 1,
  STATE_BASIC = 1,
  STATE_NTSC = 2,
  STATE_PAL = 4,
  STATE_MAX_BLOB = 256 * 1024 * 1024
};

static uint32_t features(void) {
  uint32_t value = 0;
#ifndef NO_BASIC
  value |= STATE_BASIC;
#endif
#ifndef NO_NTSC
  value |= STATE_NTSC;
#endif
#ifndef NO_PAL
  value |= STATE_PAL;
#endif
  return value;
}

int State_Save(const Atari *a, const char *path) {
  if (!a || !path || (a->disk_size && !a->disk) ||
      (a->xex_size && !a->xex_data))
    return -1;
  FILE *file = fopen(path, "wb");
  if (!file)
    return -1;
  StateHeader header = {{'A', '8', '0', '0', 'S', 'T', 'A', 'T'},
                        STATE_VERSION,
                        sizeof(*a),
                        features(),
                        (uint32_t)a->video_standard,
                        a->disk_size,
                        a->xex_size};
  Atari image = *a;
  image.palette = NULL;
  image.disk = image.xex_data = NULL;
  image.line_sink = NULL;
  image.line_defer = NULL;
  image.line_sink_context = NULL;
  int failed =
      fwrite(&header, sizeof(header), 1, file) != 1 ||
      fwrite(&image, sizeof(image), 1, file) != 1 ||
      (a->disk_size &&
       fwrite(a->disk, 1, a->disk_size, file) != a->disk_size) ||
      (a->xex_size && fwrite(a->xex_data, 1, a->xex_size, file) != a->xex_size);
  if (fclose(file))
    failed = 1;
  return failed ? -1 : 0;
}

int State_Load(Atari *a, const char *path) {
  if (!a || !path)
    return -1;
  FILE *file = fopen(path, "rb");
  if (!file)
    return -1;
  StateHeader header = {{0}};
  Atari image = {0};
  uint8_t *disk = NULL, *xex = NULL;
  int failed =
      fread(&header, sizeof(header), 1, file) != 1 ||
      memcmp(header.magic, "A800STAT", 8) || header.version != STATE_VERSION ||
      header.atari_size != sizeof(image) || header.features != features() ||
      header.standard > A800_PAL || header.disk_size > STATE_MAX_BLOB ||
      header.xex_size > STATE_MAX_BLOB ||
      fread(&image, sizeof(image), 1, file) != 1 ||
      image.disk_size != header.disk_size ||
      image.xex_size != header.xex_size || image.xex_pos > image.xex_size ||
      image.video_standard != (AtariVideoStandard)header.standard;
#ifdef NO_NTSC
  failed |= header.standard == A800_NTSC;
#endif
#ifdef NO_PAL
  failed |= header.standard == A800_PAL;
#endif
  if (!failed && header.disk_size) {
    disk = (uint8_t *)malloc((size_t)header.disk_size);
    failed = !disk || fread(disk, 1, (size_t)header.disk_size, file) !=
                          (size_t)header.disk_size;
  }
  if (!failed && header.xex_size) {
    xex = (uint8_t *)malloc((size_t)header.xex_size);
    failed = !xex || fread(xex, 1, (size_t)header.xex_size, file) !=
                         (size_t)header.xex_size;
  }
  if (file && fclose(file))
    failed = 1;
  if (failed) {
    free(disk);
    free(xex);
    return -1;
  }
  free(a->disk);
  free(a->xex_data);
  Atari_InvalidateMemoryMap(&image);
  image.line_sink = a->line_sink;
  image.line_defer = a->line_defer;
  image.line_sink_context = a->line_sink_context;
  *a = image;
  a->disk = disk;
  a->xex_data = xex;
  a->palette = Gtia_Palette(a->video_standard);
  a->audio_len = 0;
  return 0;
}
