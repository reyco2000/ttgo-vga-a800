#include "Atari.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int extension_is(const char *path, const char *extension) {
  size_t a = strlen(path), b = strlen(extension);
  if (a < b)
    return 0;
  path += a - b;
  while (*path) {
    char x = *path++, y = *extension++;
    if (x >= 'A' && x <= 'Z')
      x = (char)(x + 32);
    if (y >= 'A' && y <= 'Z')
      y = (char)(y + 32);
    if (x != y)
      return 0;
  }
  return 1;
}
static int write_ppm(const Atari *a, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return -1;
  fprintf(f, "P6\n%d %d\n255\n", A800_WIDTH, a->frame_lines);
  for (int i = 0; i < A800_WIDTH * a->frame_lines; i++) {
    uint32_t p = a->pixels[i];
    fputc((int)(p >> 16) & 255, f);
    fputc((int)(p >> 8) & 255, f);
    fputc((int)p & 255, f);
  }
  return fclose(f);
}
static void usage(const char *name) {
  fprintf(stderr,
          "usage: %s [--pal|--ntsc] [--basic] [--frames N] [--xex-frame N] "
          "[--start-frame N] [--trace-audio] [--output frame.ppm] [--os ROM] "
          "[--basic-rom ROM] [media.atr|program.xex]\n",
          name);
}

int main(int argc, char **argv) {
  const char *os = "ATARIXL.ROM", *basic_rom = "ATARIBAS.ROM", *media = NULL,
             *output = NULL;
  int frames = 180, xex_frame = 76, start_frame = -1, basic = 0,
      trace_audio = 0;
  AtariVideoStandard standard = A800_NTSC;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--pal"))
      standard = A800_PAL;
    else if (!strcmp(argv[i], "--ntsc"))
      standard = A800_NTSC;
    else if (!strcmp(argv[i], "--basic"))
      basic = 1;
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
      frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--xex-frame") && i + 1 < argc)
      xex_frame = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--start-frame") && i + 1 < argc)
      start_frame = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--trace-audio"))
      trace_audio = 1;
    else if (!strcmp(argv[i], "--output") && i + 1 < argc)
      output = argv[++i];
    else if (!strcmp(argv[i], "--os") && i + 1 < argc)
      os = argv[++i];
    else if (!strcmp(argv[i], "--basic-rom") && i + 1 < argc)
      basic_rom = argv[++i];
    else if (argv[i][0] == '-') {
      usage(argv[0]);
      return 2;
    } else
      media = argv[i];
  }
  if (frames < 1) {
    usage(argv[0]);
    return 2;
  }
  Atari a;
  if (Atari_Init(&a, os, basic_rom)) {
    fprintf(stderr, "a800-validate: ROM load failed\n");
    return 2;
  }
  Atari_SetVideoStandard(&a, standard);
  int xex = media && extension_is(media, ".xex");
  if (!basic && !xex)
    a.console &= (uint8_t)~4;
  if (media && !xex && Atari_LoadDisk(&a, media)) {
    fprintf(stderr, "a800-validate: media load failed: %s\n", media);
    Atari_Destroy(&a);
    return 2;
  }
  int loaded = !xex;
  uint8_t last_audio[9] = {0};
  for (int frame = 0; frame < frames; frame++) {
    if (frame == start_frame)
      a.console &= (uint8_t)~1;
    if (frame == start_frame + 1)
      a.console |= 1;
    Atari_Frame(&a);
    if (xex && !loaded && frame == xex_frame - 1) {
      if (Atari_LoadExecutable(&a, media)) {
        fprintf(stderr, "a800-validate: XEX load failed: %s\n", media);
        Atari_Destroy(&a);
        return 2;
      }
      loaded = 1;
    }
    if (trace_audio && memcmp(last_audio, a.pokey, 9)) {
      printf("audio-frame=%d regs=%02x/%02x,%02x/%02x,%02x/%02x,%02x/%02x "
             "ctl=%02x\n",
             frame, a.pokey[0], a.pokey[1], a.pokey[2], a.pokey[3], a.pokey[4],
             a.pokey[5], a.pokey[6], a.pokey[7], a.pokey[8]);
      memcpy(last_audio, a.pokey, 9);
    }
  }
  uint32_t first = a.pixels[0];
  unsigned different = 0;
  for (int i = 0; i < A800_WIDTH * a.frame_lines; i++)
    if (a.pixels[i] != first)
      different++;
  printf("standard=%s frames=%d cycles=%llu\n",
         a.video_standard == A800_PAL ? "PAL" : "NTSC", frames,
         (unsigned long long)a.cycles);
  printf("cpu pc=%04x a=%02x x=%02x y=%02x s=%02x p=%02x irq=%d nmi=%d\n", a.pc,
         a.a, a.x, a.y, a.s, a.p, a.irq, a.nmi);
  printf("memory portb=%02x os=%s basic=%s\n", Atari_PortB(&a),
         (Atari_PortB(&a) & 1) ? "on" : "off",
         (Atari_PortB(&a) & 2) ? "off" : "on");
  printf("antic dmactl=%02x dlist=%02x%02x chbase=%02x pmbase=%02x nmien=%02x "
         "nmist=%02x\n",
         a.antic[0], a.antic[3], a.antic[2], a.antic[9], a.antic[7],
         a.antic[14], a.antic[15]);
  printf("gtia colors=%02x,%02x,%02x,%02x background=%02x prior=%02x "
         "gractl=%02x\n",
         a.gtia[22], a.gtia[23], a.gtia[24], a.gtia[25], a.gtia[26], a.gtia[27],
         a.gtia[29]);
  printf("collisions m-pf=%02x,%02x,%02x,%02x p-pf=%02x,%02x,%02x,%02x "
         "m-pl=%02x,%02x,%02x,%02x p-pl=%02x,%02x,%02x,%02x\n",
         a.gtia_collisions[0], a.gtia_collisions[1], a.gtia_collisions[2],
         a.gtia_collisions[3], a.gtia_collisions[4], a.gtia_collisions[5],
         a.gtia_collisions[6], a.gtia_collisions[7], a.gtia_collisions[8],
         a.gtia_collisions[9], a.gtia_collisions[10], a.gtia_collisions[11],
         a.gtia_collisions[12], a.gtia_collisions[13], a.gtia_collisions[14],
         a.gtia_collisions[15]);
  printf("pokey aud=%02x/%02x,%02x/%02x,%02x/%02x,%02x/%02x audctl=%02x "
         "irqen=%02x irqst=%02x skctl=%02x skstat=%02x\n",
         a.pokey[0], a.pokey[1], a.pokey[2], a.pokey[3], a.pokey[4], a.pokey[5],
         a.pokey[6], a.pokey[7], a.pokey[8], a.pokey_irqen, a.pokey_irqst,
         a.pokey_skctl, a.pokey_skstat);
  printf("framebuffer=%dx%d pixels_different_from_corner=%u media=%s\n",
         A800_WIDTH, a.frame_lines, different, media ? media : "none");
  if (output && write_ppm(&a, output)) {
    fprintf(stderr, "a800-validate: cannot write %s\n", output);
    Atari_Destroy(&a);
    return 2;
  }
  Atari_Destroy(&a);
  return a.antic[0] & 3 ? 0 : 1;
}
