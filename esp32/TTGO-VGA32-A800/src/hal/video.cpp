/* FabGL VGA output: each finished ANTIC line is cropped, mapped through a
 * palette LUT and stored straight into the scanned-out framebuffer. */
#include "fabgl.h"
#include "hal.h"
#include "../../config.h"
extern "C" {
#include "../core/Antic.h"
}

/* 320x240 double-scan timing (FabGL QVGA_320x240_60Hz) widened to 336
 * active pixels: same 31.5 kHz line rate and 60 Hz frame rate. */
#define VGA_336x240_60HzD                                                      \
  "\"336x240@60HzD\" 13.23 336 344 392 420 240 245 246 262 -HSync -VSync "     \
  "DoubleScan"

static fabgl::VGAController vga;
static uint8_t lut[256];

/* FabGL stores each 32-bit word's pixels in the order 2, 3, 0, 1
 * (VGA_PIXELINROW's X ^ 2), so four pixels are packed per store. */
static void IRAM_ATTR line_sink(void *context, int y, const uint8_t *line) {
  const Atari *a = (const Atari *)context;
  int row = y - (a->frame_lines - VIDEO_HEIGHT) / 2;
  if ((unsigned)row >= (unsigned)VIDEO_HEIGHT)
    return;
  uint32_t *dst = (uint32_t *)vga.getScanline(row);
  const uint8_t *src = line + VIDEO_CROP_X;
  for (int x = 0; x < VIDEO_WIDTH; x += 4, src += 4)
    *dst++ = (uint32_t)lut[src[2]] | (uint32_t)lut[src[3]] << 8 |
             (uint32_t)lut[src[0]] << 16 | (uint32_t)lut[src[1]] << 24;
}

#if DRAW_ON_CORE1
/* Lines without player/missile graphics are drawn on core 1 while core 0
 * emulates on: core 0 queues each prepared line, the task below draws it
 * and stores it in the framebuffer. One producer, one consumer; the queue
 * bounds how far drawing lags, well under a frame, so a row is never
 * overwritten by an older line. */
/* A real memw for the cross-core hand-off: with A800_NO_PSRAM_MEMW the
 * mnemonic assembles to nothing, and __sync_synchronize() emits it. */
#define MEMORY_BARRIER() __asm__ volatile(".byte 0xc0, 0x20, 0x00" ::: "memory")
enum { QUEUE_LINES = 64 };
static AnticLine *queue; /* from the heap, after the framebuffer */
static volatile uint32_t queued, drawn;
static volatile bool drawer_idle;
static volatile uint32_t drawer_busy_us;
static TaskHandle_t drawer;

static void IRAM_ATTR defer_line(void *context, const AnticLine *line) {
  (void)context;
  while (queued - drawn >= QUEUE_LINES)
    ; /* core 1 is a full queue behind: wait for a slot */
  queue[queued % QUEUE_LINES] = *line;
  MEMORY_BARRIER();
  queued = queued + 1;
  if (drawer_idle)
    xTaskNotifyGive(drawer);
}

static void draw_task(void *context) {
  uint8_t pixels[A800_WIDTH];
  for (;;) {
    if (drawn == queued) {
      drawer_idle = true;
      MEMORY_BARRIER();
      if (drawn == queued)
        ulTaskNotifyTake(pdTRUE, 1);
      drawer_idle = false;
      continue;
    }
    const AnticLine *line = &queue[drawn % QUEUE_LINES];
    int64_t start = esp_timer_get_time();
    Antic_DrawLine(line, pixels, NULL, NULL);
    line_sink(context, line->y, pixels);
    drawer_busy_us = drawer_busy_us + (uint32_t)(esp_timer_get_time() - start);
    MEMORY_BARRIER();
    drawn = drawn + 1;
  }
}
#endif

uint32_t Video_DrawerBusyUs(void) {
#if DRAW_ON_CORE1
  return drawer_busy_us;
#else
  return 0;
#endif
}

void Video_Pause(void) {
#if DRAW_ON_CORE1
  while (drawn != queued)
    ;
  MEMORY_BARRIER();
#endif
}

void Video_Resume(void) {}

uint8_t *Video_Row(int y) { return vga.getScanline(y); }

uint8_t Video_Colour(uint8_t atari_colour) { return lut[atari_colour]; }

static uint8_t channel(uint32_t value) { return (uint8_t)((value * 3 + 127) / 255); }

bool Video_Init(Atari *a) {
  vga.begin((gpio_num_t)PIN_VGA_R1, (gpio_num_t)PIN_VGA_R0,
            (gpio_num_t)PIN_VGA_G1, (gpio_num_t)PIN_VGA_G0,
            (gpio_num_t)PIN_VGA_B1, (gpio_num_t)PIN_VGA_B0,
            (gpio_num_t)PIN_VGA_HSYNC, (gpio_num_t)PIN_VGA_VSYNC);
  vga.setResolution(VGA_336x240_60HzD);
  if (vga.getViewPortWidth() != VIDEO_WIDTH ||
      vga.getViewPortHeight() != VIDEO_HEIGHT) {
    Serial.printf("a800: VGA viewport %dx%d, largest block %u\n",
                  vga.getViewPortWidth(), vga.getViewPortHeight(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    return false;
  }
  for (int i = 0; i < 256; i++) {
    uint32_t c = a->palette[i];
    lut[i] = vga.createRawPixel(fabgl::RGB222(
        channel(c >> 16 & 255), channel(c >> 8 & 255), channel(c & 255)));
  }
  /* Sixteen hue bars over a luminance ramp, to check the monitor syncs. */
  for (int y = 0; y < VIDEO_HEIGHT; y++) {
    uint8_t *row = vga.getScanline(y);
    for (int x = 0; x < VIDEO_WIDTH; x++)
      VGA_PIXELINROW(row, x) =
          lut[(x * 16 / VIDEO_WIDTH) << 4 | (y * 8 / VIDEO_HEIGHT) << 1];
  }
  delay(TEST_PATTERN_MS);
  a->line_sink_context = a;
  a->line_sink = line_sink;
#if DRAW_ON_CORE1
  queue = (AnticLine *)heap_caps_malloc(QUEUE_LINES * sizeof(AnticLine),
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!queue ||
      xTaskCreatePinnedToCore(draw_task, "draw", 4096, a, 1, &drawer, 1) !=
          pdPASS) {
    Serial.println("a800: no memory for the core 1 drawer");
    return false;
  }
  a->line_defer = defer_line;
#endif
  return true;
}
