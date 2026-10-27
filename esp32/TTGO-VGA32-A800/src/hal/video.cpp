/* FabGL VGA output: each finished ANTIC line is cropped, mapped through a
 * palette LUT and stored straight into the scanned-out framebuffer. */
#include "fabgl.h"
#include "hal.h"
#include "../../config.h"

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

static uint8_t channel(uint32_t value) { return (uint8_t)((value * 3 + 127) / 255); }

bool Video_Init(Atari *a) {
  vga.begin((gpio_num_t)PIN_VGA_R1, (gpio_num_t)PIN_VGA_R0,
            (gpio_num_t)PIN_VGA_G1, (gpio_num_t)PIN_VGA_G0,
            (gpio_num_t)PIN_VGA_B1, (gpio_num_t)PIN_VGA_B0,
            (gpio_num_t)PIN_VGA_HSYNC, (gpio_num_t)PIN_VGA_VSYNC);
  vga.setResolution(VGA_336x240_60HzD);
  if (vga.getViewPortWidth() != VIDEO_WIDTH ||
      vga.getViewPortHeight() != VIDEO_HEIGHT)
    return false;
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
  return true;
}
