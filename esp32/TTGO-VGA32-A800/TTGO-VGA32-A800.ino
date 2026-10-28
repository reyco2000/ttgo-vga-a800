/* a800 frontend for the LILYGO TTGO VGA32: VGA via FabGL, PS/2 keyboard,
 * DAC1 audio and ROM/media files from microSD. Build with build.sh, which
 * copies the shared core into src/core and sets the core build options. */
#include "config.h"
#include "src/hal/hal.h"
#include "src/ui/osd.h"
#include "src/ui/settings.h"
#include "src/ui/supervisor.h"
extern "C" {
#include "src/core/6502.h"
#include "src/core/Antic.h"
#include "src/core/Pokey.h"
#include "src/core/AtariIo.h"
}

/* Two frames of audio queued ahead of the DAC paces emulation. */
enum { AUDIO_TARGET = 2 * AUDIO_RATE / 60 };

/* The ~94 KB machine comes from the internal heap before FabGL takes its
 * framebuffer; static DRAM is too small to hold it. */
static Atari *atari;
static Settings cfg; /* running configuration, edited by the supervisor */
static OsChoice booted_os;
static char media[128];
/* The mounted XEX is kept in RAM: its deferred load runs after the SD card
 * has been unmounted. */
static uint8_t *xex_image;
static size_t xex_image_size;
static bool xex_armed;
static int use_basic, option_boot_frames, boot_frames;
static float last_fps;

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

static bool file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f)
    fclose(f);
  return f != NULL;
}

/* Reads an XEX into xex_image (PSRAM for anything sizeable). */
static bool read_xex(const char *path) {
  FILE *f = fopen(path, "rb");
  long n = -1;
  uint8_t *data = NULL;
  if (f && !fseek(f, 0, SEEK_END) && (n = ftell(f)) >= 6 && !fseek(f, 0, SEEK_SET))
    data = (uint8_t *)malloc((size_t)n);
  bool ok = data && fread(data, 1, (size_t)n, f) == (size_t)n;
  if (f)
    fclose(f);
  if (!ok) {
    free(data);
    return false;
  }
  free(xex_image);
  xex_image = data;
  xex_image_size = (size_t)n;
  return true;
}

/* The F5 reset: cold start keeping cartridge and disk, OPTION held when
 * BASIC is off, and the mounted XEX loaded again after the boot frames. */
void Host_ColdReset(void) {
  Atari_Reset(atari);
  Pokey_Audio(atari, NULL, 0, AUDIO_RATE); /* Atari_Reset clears the rate */
  boot_without_basic();
  xex_armed = xex_image != NULL;
  boot_frames = 0;
}

void Host_Eject(void) {
  Atari_EjectDisk(atari);
  free(xex_image);
  xex_image = NULL;
  xex_image_size = 0;
  xex_armed = false;
  media[0] = 0;
}

int Host_Mount(const char *path) {
  if (is_xex(path)) {
    if (!read_xex(path))
      return -1;
    Atari_EjectDisk(atari);
  } else {
    if (Atari_LoadDisk(atari, path))
      return -1;
    free(xex_image);
    xex_image = NULL;
    xex_image_size = 0;
  }
  snprintf(media, sizeof(media), "%s", path);
  Host_ColdReset();
  return 0;
}

const char *Host_Media(void) { return media; }

/* "" removes the cartridge; BASIC then goes back in on an 800 OS. */
int Host_Cart(const char *path) {
  if (path[0])
    return AtariIo_LoadCartridge(atari, path);
  Atari_RemoveCartridge(atari);
  if (atari->machine_type == A800_MACHINE_800 && use_basic &&
      AtariIo_LoadCartridge(atari, BASIC_ROM)) {
    Serial.printf("a800: cannot load " BASIC_ROM ", BASIC off\n");
    use_basic = 0;
  }
  return 0;
}

OsChoice Host_BootedOs(void) { return booted_os; }
bool Host_Basic(void) { return use_basic; }
float Host_Fps(void) { return last_fps; }

/* Builds the machine from the settings, with the SD card mounted: OS and
 * BASIC, video standard, cartridge, media, then the VGA output (started
 * once, here). A game cartridge wins over BASIC as a cartridge. A cartridge
 * that fails to load is dropped from s, the running configuration. */
static void boot_machine(Settings *s) {
  OsChoice os = s->os;
  bool xl = os == OS_XL;
  use_basic = s->basic;
  int r = Atari_Init(atari, Settings_OsPath(os), xl && use_basic ? BASIC_ROM : NULL);
  if (r == -1 && !xl) {
    Serial.printf("a800: cannot load %s, using the XL OS\n", Settings_OsPath(os));
    os = OS_XL, xl = true;
    r = Atari_Init(atari, Settings_OsPath(os), use_basic ? BASIC_ROM : NULL);
  }
  if (r == -2 || r == -3) {
    Serial.printf("a800: %s, booting without BASIC\n",
                  r == -2 ? "cannot load " BASIC_ROM : "BASIC requires an XL OS");
    use_basic = 0;
    r = Atari_Init(atari, Settings_OsPath(os), NULL);
  }
  if (r) {
    Serial.printf("a800: cannot load %s%s\n",
                  r == -1 ? Settings_OsPath(os) : BASIC_ROM,
                  r == -3 ? " (BASIC requires an XL OS)" : "");
    halt("ROM load failed");
  }
  Atari_SetVideoStandard(atari, s->pal ? A800_PAL : A800_NTSC);
  booted_os = os;
  if (s->cart[0] && AtariIo_LoadCartridge(atari, s->cart)) {
    Serial.printf("a800: warning: cannot load cartridge %s\n", s->cart);
    s->cart[0] = 0;
  } else if (s->cart[0]) {
    Serial.printf("a800: cartridge %s\n", s->cart);
  }
  if (!s->cart[0] && !xl && use_basic &&
      AtariIo_LoadCartridge(atari, BASIC_ROM)) {
    Serial.printf("a800: cannot load " BASIC_ROM ", booting without BASIC\n");
    use_basic = 0;
  }
  boot_without_basic();
  char autorun[128] = "";
  Storage_ReadAutorun(autorun, sizeof(autorun));
  Settings_ChooseMedia(s, file_exists, autorun, media, sizeof(media));
  if (media[0]) {
    if (is_xex(media) ? !read_xex(media) : Atari_LoadDisk(atari, media) != 0) {
      Serial.printf("a800: warning: cannot mount %s\n", media);
      media[0] = 0;
    }
    xex_armed = xex_image != NULL;
    Serial.printf("a800: media %s\n", media);
  }
  Serial.printf("a800: %s %s, BASIC %s\n", Atari_MachineName(atari),
                atari->video_standard == A800_PAL ? "PAL" : "NTSC",
                use_basic ? "on" : "off");
  if (!Video_Init(atari))
    halt("cannot allocate the VGA framebuffer");
}

#if BENCH_AT_BOOT
/* Prices the pieces of a frame on the board itself. Machine state is
 * disturbed, so the machine is reset afterwards. */
static float bench(const char *label) {
  int64_t start = esp_timer_get_time();
  for (int i = 0; i < 60; i++)
    Atari_Frame(atari);
  float ms = (esp_timer_get_time() - start) / 60000.0f;
  Serial.printf("bench %-10s %6.2f ms/frame\n", label, ms);
  atari->audio_len = 0;
  return ms;
}
/* Host cycles per call. */
static void cycle_bench(void) {
  uint32_t t = cpu_hal_get_cycle_count(), emulated = 0;
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
  AnticLine line;
  t = cpu_hal_get_cycle_count();
  for (int i = 0; i < 2000; i++)
    Antic_PrepareLine(atari, 100, &line);
  uint32_t prepare = cpu_hal_get_cycle_count() - t;
  /* POKEY's per-sample DC filter, in the same double arithmetic. */
  volatile double previous = 0, filter = 0;
  volatile int16_t out = 0;
  t = cpu_hal_get_cycle_count();
  for (int i = 0; i < 2000; i++) {
    double sum = (double)(uint32_t)(i * 7 % 61) / (61 + (i & 7));
    double filtered = sum - previous + 0.995 * filter;
    previous = sum;
    filter = filtered;
    if (filtered > 40)
      filtered = 40;
    if (filtered < -40)
      filtered = -40;
    out = (int16_t)(filtered * 500.0);
  }
  uint32_t sample = cpu_hal_get_cycle_count() - t;
  Serial.printf("bench cycles: Cpu_Step %u (%u per emulated cycle), "
                "Pokey_Clock %u, CaptureLine %u, PrepareLine %u, "
                "sample filter %u\n",
                (unsigned)(step / 20000), (unsigned)(step / emulated),
                (unsigned)(clock / 20000), (unsigned)(capture / 2000),
                (unsigned)(prepare / 2000), (unsigned)(sample / 2000));
}
static void run_bench(void) {
  for (int i = 0; i < 120; i++)
    Atari_Frame(atari), atari->audio_len = 0;
  cycle_bench();
  bench("full");
  int rate = atari->audio_rate;
  atari->audio_rate = 0;
  bench("no audio");
  atari->audio_rate = rate;
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
  Settings_Load(&cfg);
  if (!Storage_Init())
    halt("cannot mount microSD");
  boot_machine(&cfg); /* halts on failure; ends with Video_Init */
  Storage_End();      /* ROMs and media are in RAM now */
#if OSD_SELFTEST
  {
    Serial.printf("osd: heap before %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Osd_Begin(atari);
    Osd_Rect(0, 0, VIDEO_WIDTH, VIDEO_HEIGHT, OSD_BG);
    Osd_Frame(8, 8, VIDEO_WIDTH - 16, VIDEO_HEIGHT - 16, OSD_FG);
    Osd_Text(8 * 11, 24, "A800 SUPERVISOR", OSD_FG, OSD_BG);
    Osd_Text(8, 40, "abc xyz {|} 0123456789 !@#$%^&*()", OSD_FG, OSD_BG);
    Osd_Text(8, 52, " SELECTED ", OSD_SEL_FG, OSD_SEL_BG);
    for (int i = 0; i < 6; i++) {
      Osd_Rect(16 + i * 52, 96, 44, 40, OSD_TILE);
      Osd_Icon(22 + i * 52, 104, (OsdIcon)i, OSD_FG, OSD_TILE);
    }
    Serial.printf("osd: heap after %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    delay(3000);
  }
#endif
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
  static uint32_t stats_start, stats_frames, stats_underruns, stats_drawer;
  static int64_t emulated_us;
  int keys = Keyboard_Poll(atari); /* goes to the menu while it is open */
  if (keys & KEY_ACTION_SUPERVISOR)
    Supervisor_Open(atari, &cfg);
  if (Supervisor_Active()) {
    Supervisor_Update();
    vTaskDelay(1);
    if (!Supervisor_Active()) {
      /* Closed: the ring drained while paused, so restart the statistics
       * (underruns) and let pacing refill it from empty. */
      stats_start = millis();
      stats_frames = 0;
      stats_underruns = Audio_Underruns();
      stats_drawer = Video_DrawerBusyUs();
      emulated_us = 0;
    }
    return;
  }
  if (keys & KEY_ACTION_RESET)
    Host_ColdReset();
  int64_t start = esp_timer_get_time();
  Atari_Frame(atari);
  if (option_boot_frames && !--option_boot_frames)
    atari->console |= 4;
  if (xex_armed && ++boot_frames == A800_XEX_BOOT_FRAMES) {
    if (Atari_LoadExecutableData(atari, xex_image, xex_image_size))
      Serial.printf("a800: cannot load executable %s\n", media);
    xex_armed = false;
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
    uint32_t underruns = Audio_Underruns(), drawer = Video_DrawerBusyUs();
    last_fps = stats_frames * 1000.0f / (now - stats_start);
    Serial.printf("a800: %.1f fps, %.2f ms/frame emulated, %u underruns, "
                  "core 1 drawing %.0f%%\n", last_fps,
                  emulated_us / 1000.0f / stats_frames,
                  (unsigned)(underruns - stats_underruns),
                  (drawer - stats_drawer) / 10.0f / (now - stats_start));
    stats_underruns = underruns;
    stats_drawer = drawer;
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
