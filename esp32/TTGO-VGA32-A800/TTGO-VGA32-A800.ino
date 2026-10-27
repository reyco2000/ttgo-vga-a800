/* a800 frontend for the LILYGO TTGO VGA32: VGA via FabGL, PS/2 keyboard,
 * DAC1 audio and ROM/media files from microSD. Build with build.sh, which
 * copies the shared core into src/core and sets the core build options. */
#include "config.h"
#include "src/hal/hal.h"
extern "C" {
#include "src/core/6502.h"
#include "src/core/Antic.h"
#include "src/core/Pokey.h"
}

/* Two frames of audio queued ahead of the DAC paces emulation. */
enum { AUDIO_TARGET = 2 * AUDIO_RATE / 60 };

/* The ~94 KB machine comes from the internal heap before FabGL takes its
 * framebuffer; static DRAM is too small to hold it. */
static Atari *atari;
static char media[128];
static const char *xex;
static int use_basic, option_boot_frames, boot_frames;

static void halt(const char *message) {
  Serial.printf("a800: %s\n", message);
  for (;;)
    delay(1000);
}

static void boot_without_basic(void) {
  if (atari->machine_type == A800_MACHINE_XL && !use_basic)
    atari->console &= (uint8_t)~4, option_boot_frames = 1;
}

static bool is_xex(const char *path) {
  size_t n = strlen(path);
  return n >= 4 && !strcasecmp(path + n - 4, ".xex");
}

#if BENCH_AT_BOOT
/* Splits the cost of a frame on the board itself: full frames, frames
 * without POKEY audio synthesis, without the VGA line sink, and ANTIC
 * rendering alone. Machine state is disturbed, so the machine is reset. */
static float bench(const char *label, int render_only) {
  int64_t start = esp_timer_get_time();
  for (int i = 0; i < 60; i++)
    render_only ? Antic_Frame(atari) : Atari_Frame(atari);
  float ms = (esp_timer_get_time() - start) / 60000.0f;
  Serial.printf("bench %-22s %6.2f ms/frame\n", label, ms);
  atari->audio_len = 0;
  return ms;
}
/* Host cycles per call for the pieces of a scanline. */
static void cycle_bench(void) {
  uint32_t t = cpu_hal_get_cycle_count();
  uint32_t emulated = 0;
  for (int i = 0; i < 20000; i++)
    emulated += (uint32_t)Cpu_Step(atari);
  uint32_t step = cpu_hal_get_cycle_count() - t;
  t = cpu_hal_get_cycle_count();
  for (int i = 0; i < 20000; i++)
    Pokey_Clock(atari, 4);
  uint32_t clock = cpu_hal_get_cycle_count() - t;
  atari->audio_pending = 0;
  t = cpu_hal_get_cycle_count();
  for (int i = 0; i < 2000; i++)
    Antic_CaptureLine(atari);
  uint32_t capture = cpu_hal_get_cycle_count() - t;
  Serial.printf("bench Cpu_Step %u cycles (%u per emulated cycle), "
                "Pokey_Clock %u, CaptureLine %u\n",
                (unsigned)(step / 20000), (unsigned)(step / emulated),
                (unsigned)(clock / 20000), (unsigned)(capture / 2000));
}
static void run_bench(void) {
  for (int i = 0; i < 120; i++)
    Atari_Frame(atari), atari->audio_len = 0;
  void (*sink)(void *, int, const uint8_t *) = atari->line_sink;
  cycle_bench();
  bench("full", 0);
  atari->audio_rate = 0;
  bench("no audio", 0);
  atari->line_sink = NULL;
  bench("no audio, no sink", 0);
  bench("ANTIC render only", 1);
  atari->line_sink = sink;
  bench("ANTIC render + sink", 1);
  Atari_Reset(atari);
  boot_without_basic();
  atari->audio_len = 0;
}
#endif

void setup() {
  Serial.begin(115200);
  Serial.printf("a800 TTGO VGA32 %s, Atari %u bytes, heap %u, PSRAM %u\n",
                FIRMWARE_VERSION, (unsigned)sizeof(*atari),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  atari = (Atari *)heap_caps_calloc(1, sizeof(*atari),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!atari) {
    Serial.printf("a800: largest internal block %u\n",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    halt("cannot allocate the machine");
  }
  if (!Storage_Init())
    halt("cannot mount microSD");
  use_basic = USE_BASIC;
  int r = Atari_Init(atari, OS_ROM, use_basic ? BASIC_ROM : NULL);
  if (r == -2 || r == -3) {
    Serial.printf("a800: %s, booting without BASIC\n",
                  r == -2 ? "cannot load " BASIC_ROM : "BASIC requires an XL OS");
    use_basic = 0;
    r = Atari_Init(atari, OS_ROM, NULL);
  }
  if (r) {
    Serial.printf("a800: cannot load %s%s\n", r == -1 ? OS_ROM : BASIC_ROM,
                  r == -3 ? " (BASIC requires an XL OS)" : "");
    halt("ROM load failed");
  }
  Atari_SetVideoStandard(atari, USE_PAL ? A800_PAL : A800_NTSC);
  boot_without_basic();
  if (Storage_ReadAutorun(media, sizeof(media))) {
    if (is_xex(media))
      xex = media;
    else if (Atari_LoadDisk(atari, media))
      Serial.printf("a800: warning: cannot mount %s\n", media);
    Serial.printf("a800: media %s\n", media);
  }
  Serial.printf("a800: %s %s, BASIC %s\n", Atari_MachineName(atari),
                atari->video_standard == A800_PAL ? "PAL" : "NTSC",
                use_basic ? "on" : "off");
#if BENCH_AT_BOOT
  Serial.printf("bench CPU %u MHz, machine at %p, before VGA:\n",
                (unsigned)getCpuFrequencyMhz(), (void *)atari);
  run_bench();
#endif
  if (!Video_Init(atari))
    halt("cannot allocate the VGA framebuffer");
  Keyboard_Init();
  Pokey_Audio(atari, NULL, 0, AUDIO_RATE);
#if BENCH_AT_BOOT
  run_bench();
  Pokey_Audio(atari, NULL, 0, AUDIO_RATE);
#endif
  Audio_Init();
  disableCore0WDT();
  if (xTaskCreatePinnedToCore(emulation_task, "emulation", 8192, NULL, 1, NULL,
                              0) != pdPASS)
    halt("cannot start the emulation task");
  Serial.printf("a800: running on core 0, heap %u\n",
                (unsigned)ESP.getFreeHeap());
}

/* One emulated frame plus audio hand-off, pacing and statistics. */
static void run_frame(void) {
  static int16_t samples[512];
  static uint32_t stats_start, stats_frames, stats_underruns;
  static int64_t emulated_us;
  if (Keyboard_Poll(atari) & KEY_ACTION_RESET) {
    Atari_Reset(atari);
    boot_without_basic();
  }
  int64_t start = esp_timer_get_time();
  Atari_Frame(atari);
  if (option_boot_frames && !--option_boot_frames)
    atari->console |= 4;
  if (xex && ++boot_frames == A800_XEX_BOOT_FRAMES) {
    if (Atari_LoadExecutable(atari, xex))
      Serial.printf("a800: cannot load executable %s\n", xex);
    xex = NULL;
  }
  emulated_us += esp_timer_get_time() - start;
  while (atari->audio_len) {
    int n = atari->audio_len < 512 ? atari->audio_len : 512;
    Pokey_Audio(atari, samples, n, AUDIO_RATE);
    Audio_Push(samples, n);
  }
  while (Audio_Fill() > AUDIO_TARGET)
    vTaskDelay(1);
  stats_frames++;
  uint32_t now = millis();
  if (now - stats_start >= STATS_PERIOD_MS) {
    uint32_t underruns = Audio_Underruns();
    Serial.printf("a800: %.1f fps, %.2f ms/frame emulated, %u underruns\n",
                  stats_frames * 1000.0f / (now - stats_start),
                  emulated_us / 1000.0f / stats_frames,
                  (unsigned)(underruns - stats_underruns));
    stats_underruns = underruns;
    stats_start = now;
    stats_frames = 0;
    emulated_us = 0;
  }
}

/* The emulator gets core 0 to itself, as in the Apple II port: the audio
 * timer interrupt, Serial and the Arduino loop stay on core 1. When it falls
 * behind it never yields, so core 0's idle-task watchdog is disabled. */
static void emulation_task(void *) {
  for (;;)
    run_frame();
}

void loop() { vTaskDelay(1000); }
