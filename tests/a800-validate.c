#include "Antic.h"
#include "Atari.h"
#include "Pokey.h"
#ifndef NO_SAVE_STATE
#include "State.h"
#endif
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
#ifdef A800_LINE_BUFFER
static uint8_t frame_pixels[A800_WIDTH * A800_MAX_HEIGHT];
static void capture_line(void *context, int y, const uint8_t *line) {
  (void)context;
  memcpy(frame_pixels + (size_t)y * A800_WIDTH, line, A800_WIDTH);
}
#endif
static const uint8_t *frame_of(const Atari *a) {
#ifdef A800_LINE_BUFFER
  (void)a;
  return frame_pixels;
#else
  return a->pixels;
#endif
}
/* --defer: draw eligible lines the way a second core would, from the
 * prepared line alone. */
static void defer_line(void *context, const AnticLine *line) {
  Atari *a = (Atari *)context;
  uint8_t pixels[A800_WIDTH];
  Antic_DrawLine(line, pixels, NULL, NULL);
#ifdef A800_LINE_BUFFER
  memcpy(frame_pixels + (size_t)line->y * A800_WIDTH, pixels, A800_WIDTH);
#else
  memcpy(a->pixels + (size_t)line->y * A800_WIDTH, pixels, A800_WIDTH);
#endif
  (void)a;
}
static uint64_t fnv1a(uint64_t hash, const void *data, size_t size) {
  const uint8_t *p = (const uint8_t *)data;
  while (size--)
    hash = (hash ^ *p++) * 0x100000001b3ull;
  return hash;
}
static int write_ppm(const Atari *a, const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return -1;
  fprintf(f, "P6\n%d %d\n255\n", A800_WIDTH, a->frame_lines);
  for (int i = 0; i < A800_WIDTH * a->frame_lines; i++) {
    uint32_t p = a->palette[frame_of(a)[i]];
    fputc((int)(p >> 16) & 255, f);
    fputc((int)(p >> 8) & 255, f);
    fputc((int)p & 255, f);
  }
  return fclose(f);
}
static void usage(const char *name) {
  fprintf(stderr,
          "usage: %s "
#if !defined(NO_NTSC) && !defined(NO_PAL)
          "[--pal|--ntsc] "
#endif
#ifndef NO_BASIC
          "[--basic] "
#endif
          "[--frames N|--seconds N] "
          "[--xex-frame N] [--start-frame N] [--trace-audio] [--audio-rate HZ] "
          "[--defer] "
          "[--output "
          "frame.ppm] "
#ifndef NO_SAVE_STATE
          "[--load-state FILE] "
#endif
          "[--os ROM] "
#ifndef NO_BASIC
          "[--basic-rom ROM] "
#endif
          "[media.atr|program.xex]\n",
          name);
}

int main(int argc, char **argv) {
  const char *os = "ATARIXL.ROM", *media = NULL, *output = NULL,
             *load_state = NULL;
#ifndef NO_BASIC
  const char *basic_rom = "ATARIBAS.ROM";
#endif
  int frames = 180, xex_frame = A800_XEX_BOOT_FRAMES, start_frame = -1,
      basic = 0, trace_audio = 0, audio_rate = 0, defer = 0;
  double seconds = 0;
#ifdef NO_NTSC
  AtariVideoStandard standard = A800_PAL;
#else
  AtariVideoStandard standard = A800_NTSC;
#endif
  for (int i = 1; i < argc; i++) {
#if !defined(NO_NTSC) && !defined(NO_PAL)
    if (!strcmp(argv[i], "--pal"))
      standard = A800_PAL;
    else if (!strcmp(argv[i], "--ntsc"))
      standard = A800_NTSC;
    else
#endif
#ifndef NO_BASIC
        if (!strcmp(argv[i], "--basic"))
      basic = 1;
    else if (!strcmp(argv[i], "--basic-rom") && i + 1 < argc)
      basic_rom = argv[++i];
    else
#endif
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
      frames = atoi(argv[++i]);
      seconds = 0;
    } else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
      seconds = atof(argv[++i]);
    else if (!strcmp(argv[i], "--xex-frame") && i + 1 < argc)
      xex_frame = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--start-frame") && i + 1 < argc)
      start_frame = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--trace-audio"))
      trace_audio = 1;
    else if (!strcmp(argv[i], "--defer"))
      defer = 1;
    else if (!strcmp(argv[i], "--audio-rate") && i + 1 < argc)
      audio_rate = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--output") && i + 1 < argc)
      output = argv[++i];
#ifndef NO_SAVE_STATE
    else if (!strcmp(argv[i], "--load-state") && i + 1 < argc)
      load_state = argv[++i];
#endif
    else if (!strcmp(argv[i], "--os") && i + 1 < argc)
      os = argv[++i];
    else if (argv[i][0] == '-') {
      usage(argv[0]);
      return 2;
    } else
      media = argv[i];
  }
  if (frames < 1 || seconds < 0 || audio_rate < 0) {
    usage(argv[0]);
    return 2;
  }
  Atari a;
  memset(&a, 0, sizeof(a));
#ifndef NO_SAVE_STATE
  if (load_state) {
    if (State_Load(&a, load_state)) {
      fprintf(stderr, "a800-validate: state load failed: %s\n", load_state);
      return 2;
    }
  } else
#endif
  {
#ifdef NO_BASIC
    if (Atari_Init(&a, os, NULL)) {
#else
    if (Atari_Init(&a, os, basic ? basic_rom : NULL)) {
#endif
      fprintf(stderr, "a800-validate: ROM load failed\n");
      return 2;
    }
    Atari_SetVideoStandard(&a, standard);
  }
#ifdef A800_LINE_BUFFER
  a.line_sink = capture_line;
#endif
  if (defer) {
    a.line_defer = defer_line;
    a.line_sink_context = &a;
  }
  if (audio_rate)
    Pokey_Audio(&a, NULL, 0, audio_rate);
  if (seconds > 0)
    frames = (int)(seconds * a.frame_rate + .5);
  int xex = !load_state && media && extension_is(media, ".xex");
  int option_boot_frames = 0;
  if (!load_state && a.machine_type == A800_MACHINE_XL && !basic)
    a.console &= (uint8_t)~4, option_boot_frames = 1;
  if (!load_state && media && !xex && Atari_LoadDisk(&a, media)) {
    fprintf(stderr, "a800-validate: media load failed: %s\n", media);
    Atari_Destroy(&a);
    return 2;
  }
  int loaded = !xex;
  uint8_t last_audio[9] = {0};
  uint64_t video_hash = 0xcbf29ce484222325ull, audio_hash = video_hash,
           audio_samples = 0;
  for (int frame = 0; frame < frames; frame++) {
    if (frame == start_frame)
      a.console &= (uint8_t)~1;
    if (frame == start_frame + 1)
      a.console |= 1;
    Atari_Frame(&a);
    if (option_boot_frames && !--option_boot_frames)
      a.console |= 4;
    video_hash = fnv1a(video_hash, frame_of(&a),
                       (size_t)A800_WIDTH * (size_t)a.frame_lines);
    while (audio_rate && a.audio_len) {
      int16_t samples[512];
      int n = a.audio_len < 512 ? a.audio_len : 512;
      Pokey_Audio(&a, samples, n, audio_rate);
      audio_hash = fnv1a(audio_hash, samples, (size_t)n * sizeof(*samples));
      audio_samples += (uint64_t)n;
    }
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
  const uint8_t *pixels = frame_of(&a);
  uint8_t first = pixels[0];
  unsigned different = 0;
  for (int i = 0; i < A800_WIDTH * a.frame_lines; i++)
    if (pixels[i] != first)
      different++;
  printf("machine=%s standard=%s frames=%d cycles=%llu\n",
         Atari_MachineName(&a), a.video_standard == A800_PAL ? "PAL" : "NTSC",
         frames, (unsigned long long)a.cycles);
  printf("cpu pc=%04x a=%02x x=%02x y=%02x s=%02x p=%02x irq=%d nmi=%d\n", a.pc,
         a.a, a.x, a.y, a.s, a.p, a.irq, a.nmi);
#ifdef NO_BASIC
  printf("memory portb=%02x os=%s basic=unavailable\n", Atari_PortB(&a),
         (Atari_PortB(&a) & 1) ? "on" : "off");
#else
  printf("memory portb=%02x os=%s basic=%s\n", Atari_PortB(&a),
         (Atari_PortB(&a) & 1) ? "on" : "off",
         (Atari_PortB(&a) & 2) ? "off" : "on");
#endif
  printf("antic dmactl=%02x dlist=%02x%02x chbase=%02x pmbase=%02x nmien=%02x "
         "nmist=%02x\n",
         a.antic[0], a.antic[3], a.antic[2], a.antic[9], a.antic[7],
         a.antic[14], a.antic[15]);
  printf("gtia colors=%02x,%02x,%02x,%02x background=%02x prior=%02x "
         "gractl=%02x\n",
         a.gtia[22], a.gtia[23], a.gtia[24], a.gtia[25], a.gtia[26], a.gtia[27],
         a.gtia[29]);
  printf("pmg hpos=%02x,%02x,%02x,%02x missiles=%02x,%02x,%02x,%02x "
         "sizes=%02x,%02x,%02x,%02x/%02x colors=%02x,%02x,%02x,%02x\n",
         a.gtia[0], a.gtia[1], a.gtia[2], a.gtia[3], a.gtia[4], a.gtia[5],
         a.gtia[6], a.gtia[7], a.gtia[8], a.gtia[9], a.gtia[10], a.gtia[11],
         a.gtia[12], a.gtia[18], a.gtia[19], a.gtia[20], a.gtia[21]);
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
  printf("hash video=%016llx audio=%016llx samples=%llu\n",
         (unsigned long long)video_hash, (unsigned long long)audio_hash,
         (unsigned long long)audio_samples);
  if (output && !different)
    printf("capture=%s skipped=blank-frame\n", output);
  if (output && different && write_ppm(&a, output)) {
    fprintf(stderr, "a800-validate: cannot write %s\n", output);
    Atari_Destroy(&a);
    return 2;
  }
  Atari_Destroy(&a);
  return different ? 0 : 1;
}
