/* Board and frontend configuration for the LILYGO TTGO VGA32 v1.4
 * (ESP32-WROVER, 4 MB PSRAM). Core build options (A800_LINE_BUFFER etc.) are
 * passed on the compiler command line by build.sh so the C core and this C++
 * frontend agree on the Atari struct layout. */
#ifndef A800_ESP32_CONFIG_H
#define A800_ESP32_CONFIG_H

#define FIRMWARE_VERSION "0.2.0"

/* VGA resistor DAC, 2 bits per channel (FabGL's TTGO defaults). */
#define PIN_VGA_R0 21
#define PIN_VGA_R1 22
#define PIN_VGA_G0 18
#define PIN_VGA_G1 19
#define PIN_VGA_B0 4
#define PIN_VGA_B1 5
#define PIN_VGA_HSYNC 23
#define PIN_VGA_VSYNC 15

/* PS/2 keyboard on port 0 (CLK 33, DATA 32), mouse on port 1 (CLK 26,
 * DATA 27) as joystick 0. */
#define PIN_PS2_KBD_CLK 33
#define PIN_PS2_KBD_DATA 32
#define PIN_PS2_MOUSE_CLK 26
#define PIN_PS2_MOUSE_DATA 27

/* Mouse joystick: counts of motion that push the stick, and the most the
 * stick holds (larger = lets go later after the mouse stops). */
#define MOUSE_JOY_THRESHOLD 4
#define MOUSE_JOY_RANGE 24

/* Internal DAC1. */
#define PIN_DAC_OUT 25

/* microSD on HSPI. GPIO2 is a strapping pin: keep the card out to flash. */
#define PIN_SD_CS 13
#define PIN_SD_MOSI 12
#define PIN_SD_MISO 2
#define PIN_SD_SCLK 14
#define SD_FREQ_HZ 4000000

/* Visible window copied from the 384-wide ANTIC line (same crop as the
 * WASMCART frontend), centred vertically on the active frame. */
#define VIDEO_WIDTH 336
#define VIDEO_HEIGHT 240
#define VIDEO_CROP_X 24
#define DRAW_ON_CORE1 1 /* draw lines without players/missiles on core 1 */

/* Audio ISR period; POKEY is resampled to 1e6 / AUDIO_PERIOD_US Hz. */
#define AUDIO_PERIOD_US 64
#define AUDIO_RATE (1000000 / AUDIO_PERIOD_US)

/* Files on the SD card (mounted at /sd): ROMs in /atari/roms, media in
 * /atari. AUTORUN_FILE holds one media file name (ATR or XEX), relative to
 * MEDIA_DIR; an empty or missing file boots the OS alone. */
#define MEDIA_DIR "/sd/atari"
#define ROM_DIR MEDIA_DIR "/roms"
#define OS_ROM ROM_DIR "/ATARIXL.ROM"
#define BASIC_ROM ROM_DIR "/ATARIBAS.ROM"
#define AUTORUN_FILE MEDIA_DIR "/autorun.txt"

/* First-boot defaults only; the saved settings (NVS) take over after. */
#define USE_BASIC 1     /* 1 = enable BASIC when BASIC_ROM is present */
#define USE_PAL 0       /* 1 = PAL timing (50 Hz emulation on 60 Hz VGA) */
#define KBD_LAYOUT 0    /* 0 US, 1 Latam Spanish, 2 Italian, 3 Brazilian ABNT2 */
#define MOUSE_SWAP 0    /* 1 = left-handed mouse: right button fires */
#define STATS_PERIOD_MS 2000
#define TEST_PATTERN_MS 2000 /* colour bars shown at boot; 0 = none */
#define OSD_SELFTEST 0 /* 1 = draw the OSD test screen for 3 s at boot */
#define BENCH_AT_BOOT 0 /* 1 = print a frame-cost breakdown, then reset */

#endif
