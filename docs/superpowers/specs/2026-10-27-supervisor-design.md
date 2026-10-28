# TTGO VGA32 Supervisor: design

Date: 2026-10-27
Branch: `esp32-ttgo-vga32-port`

## Intent

The a800 Atari emulator runs on the LILYGO TTGO VGA32 board
(`esp32/TTGO-VGA32-A800/`). Today the only way to change software is to edit
`/atari/autorun.txt` on the SD card and reset, and the SD card has to come out
of the board for every flash. The user wants an on-screen **supervisor**: a
menu opened on the running Atari to mount ATR disks and load cartridges
without touching the card. It also needs About, Reset and Resume.

The model is the supervisor in the user's own TTGO-VGA32-COCO project
(`/home/pi/proyectos/ESP32TTGO/coco3/TTGO-VGA32-COCO/src/supervisor/`,
documented in its `docs/supervisor.md`). The Atari version takes its look and
controls, at a much smaller scope.

## What the user decided

- **Purpose:** the two everyday jobs are mounting ATR disks and loading
  cartridges. The menu must also offer About, Reset and Resume.
- **Look:** layout A from the mockups: a full-screen panel in Atari blue and
  white, with a **3x2 grid of icon tiles**: Disks, Cart, Reset, Setup, About,
  Resume. It is styled after the CoCo supervisor's tile menu. The mockup is in
  `.superpowers/brainstorm/*/content/main-menu.html`.
- The user approved these proposals as stated:
  - **F12** opens and closes the supervisor. ESC goes back one level; arrows
    and ENTER navigate.
  - **Disks** browses `/atari/` (folders allowed) for `.atr` and `.xex`. It
    mounts the chosen ATR as D1 or loads the XEX, shows what is mounted, and
    offers eject.
  - **Cart** browses `/atari/` for `.rom` and `.car`. "Remove cartridge" is
    the first row.
  - Mounting a disk or inserting or removing a cartridge **asks whether to
    reboot**, defaulting to No (CoCo convention), then cold-starts.
  - **Reset** asks warm or cold, defaulting to No.
  - **Setup** holds the OS ROM choice, BASIC on/off, and NTSC/PAL. The OS
    choices are XL (`ATARIXL.ROM`), 800 OS-B (`ATARIOSB.ROM`) and 800 OS-A
    (`ATARIOSA.ROM`); the user has the original OS-A, OS-B and BASIC ROMs.
  - **BASIC with an 800 OS loads as a cartridge**, as on the real machine:
    `ATARIBAS.ROM` is inserted as an 8 KB cart at $A000 through the new
    cartridge support. With the XL OS, BASIC stays the built-in PORTB-banked
    ROM, as today. If a game cartridge is also inserted, the game cartridge
    wins, and BASIC is not inserted.
  - **About** shows firmware version, machine, ROMs in use, free memory and fps.
  - **Persistence:** the last disk, cartridge and Setup choices are saved in
    the ESP32's NVS flash and restored at power-on. `autorun.txt` becomes a
    fallback used only when NVS holds nothing.
  - **Cartridge support in version 1:** raw `.rom` dumps of 8 KB (mapped at
    $A000-$BFFF, e.g. the user's `Alien Garden.rom`) and 16 KB
    ($8000-$BFFF), plus `.car` files of the same two types (`.car` = 16-byte
    header naming the type, then the dump). The user will provide details of
    other cartridges later.
  - **Unsupported files** (e.g. a bank-switched `.car` type) show a short "not
    supported" message instead of failing.

## Goals

1. Mount and eject ATR disks, and load XEX files, from the SD card at runtime.
2. Insert and remove standard 8 KB / 16 KB cartridges at runtime.
3. Reset (warm or cold), resume, and an About screen.
4. Choose OS ROM, BASIC and video standard in Setup, saved across power-offs.
5. Same feel as the CoCo supervisor: tile main menu, list screens,
   No-by-default confirmation popups.

## Non-goals (version 1)

- Writing ATR changes back to the SD card. Disks stay read-only images in
  RAM, as they are today.
- Bank-switched cartridges (XEGS, OSS, Atarimax, SpartaDOS X, ...).
- The CoCo's key mapper, WiFi/debug screens, joystick setup, FujiNet.
- Drives D2-D4.
- Changing anything in the SDL or WASMCART frontends.

## Anti-goals: failures even if it "works"

- **Slowing the emulator while the menu is closed.** The board just reached
  ~59 fps on a 59.92 fps target after a long optimization effort. The closed
  supervisor must cost essentially nothing per frame.
- **Breaking bit-exactness of the shared core.** Every core change must keep
  the existing `a800-validate` scenario hashes identical when no cartridge is
  inserted, in both full-frame and `A800_LINE_BUFFER` builds and with
  `--defer`. All `ctest` tests must still pass.
- **Corrupting the screen on resume.** The emulator's next frame must fully
  repaint over the menu.
- Losing the user's ROM/disk setup to an unreadable NVS entry; fall back
  safely instead.

## Constraints and facts the builder needs

**Toolchain and build.**
- arduino-cli with `esp32:esp32@2.0.17` and FabGL 1.0.9. FabGL does not
  build on core 3.x.
- FQBN `esp32:esp32:esp32wrover:PartitionScheme=huge_app`.
- `esp32/TTGO-VGA32-A800/build.sh` copies the shared core into `src/core/`
  and passes the core flags. C and C++ must see identical flags, because the
  `Atari` struct layout depends on them.
- Flashing requires the SD card to be out of the slot. The card's pull-up on
  GPIO2 blocks download mode ("Wrong boot mode (0xa)").

**Display.**
- FabGL `VGAController`, 64 colours (RGB222), custom 336x240 double-scanned
  mode (`src/hal/video.cpp`).
- The emulator writes framebuffer rows directly through `line_sink`. Lines
  without player/missile graphics are drawn by a task on **core 1**
  (`draw_task`, fed by a 64-line queue via `line_defer`).
- Before the supervisor draws, the core-1 queue must be drained so no late
  emulator line lands on the menu.
- The CoCo project draws its OSD with a FabGL `Canvas` through `osd_canvas.*`.
  That is the proven path on this hardware.

**Tasks and pausing.**
- Emulation runs in `emulation_task` pinned to core 0 (`run_frame()` in the
  `.ino`), with the core-0 watchdog disabled. The Arduino `loop()` on core 1
  is idle.
- Audio is a timer ISR on core 1 draining a ring that `run_frame()` fills. A
  paused emulator means the ring empties and the ISR holds its last level.
  Silence is fine; clicks should be avoided if easy.

**Memory.**
- Internal heap is about 41 KB free at runtime. PSRAM has 4 MB, mostly free.
- `malloc` above 16 KB goes to PSRAM. Large buffers such as file lists and
  cartridge images may live in PSRAM. Note that PSRAM *stores* need the
  CPU-3.9 workaround: see the next item.
- **IRAM is nearly full** (about 126.9 KB of the ~129.7 KB usable). Supervisor
  code must run from flash; do not mark it `IRAM_ATTR`/`A800_HOT`.
- `A800_NO_PSRAM_MEMW` (set by `build.sh`) removes the CPU-3.9 PSRAM `memw`
  workaround in every file that includes `Atari.h`. A file that **stores** to
  PSRAM must define `A800_PSRAM_STORES` before its includes, as `AtariIo.c`
  does. Supervisor files that fill PSRAM buffers (file lists, cartridge
  loading) need this. Loads from PSRAM need nothing.

**SD card.**
- Mounted at `/sd` on HSPI (`src/hal/storage.cpp`). The firmware now
  **unmounts it after boot** (`Storage_End()`), so the supervisor must mount
  it when it needs the card and may unmount again when it closes.
- The core loads files through stdio `fopen`.
- Card layout: ROMs in `/atari/roms/` (`ATARIXL.ROM`, `ATARIOSB.ROM`,
  `ATARIOSA.ROM`, `ATARIBAS.ROM`), media in `/atari/`.

**Core API today.**
- `Atari_Init()` memsets the whole machine. That clears `line_sink`,
  `line_defer` and `line_sink_context`, which the frontend must re-install
  (`Video_Init` sets them).
- `Atari_LoadDisk()` and `Atari_LoadExecutable()` replace the current media.
  The XEX path relies on the boot logic in `run_frame()`
  (`A800_XEX_BOOT_FRAMES`, `option_boot_frames`).
- BASIC is only supported with the XL OS. `Atari_Init` returns -3 for BASIC
  with a non-XL OS, and the firmware then boots without BASIC.
- **Memory map changes must call `Atari_InvalidateMemoryMap()`.** Reads and
  writes go through lazily filled per-page tables (`read_map`/`write_map` in
  `Atari.c`). Inserting or removing a cartridge changes the map, so it must
  invalidate them.

**Cartridge hardware behaviour to emulate (standard carts).**
- An 8 KB cartridge appears at $A000-$BFFF; a 16 KB one at $8000-$BFFF.
  Cartridge space is ROM, so writes to it are ignored.
- On an XL/XE, an inserted cartridge takes priority over built-in BASIC in
  $A000-$BFFF.
- On XL/XE machines GTIA's TRIG3 reports whether a cartridge is present. The
  OS checks it, so the emulated value must match an inserted cart.
- The OS reads $BFFC (cart-present flag) and $BFFA/$BFFE (run/init vectors)
  at boot. With a correct memory map this needs nothing extra.

**Exactness tooling (desktop).**
- `tests/a800-validate.c` (hashes with `--audio-rate`, `--defer`),
  `tests/core_test.c`, `tests/pokey_reference_test.c`.
- Baseline outputs come from eight scenarios. The script and ROMs used so far
  live in the session scratchpad. They are AltirraOS/BASIC extracted from
  `/home/pi/proyectos/atari800/atari800/src/roms/*.c`, plus
  `/home/pi/proyectos/atari800/atari800/test/acid800.atr`.
- New cartridge code needs its own tests (map, priority over BASIC, TRIG3,
  eject restores the old map), because the scenarios run with no cartridge.

## Calls made by the designer (not confirmed by the user)

These came out of the design review and were settled without asking the
user. The user may override any of them.

- **Cartridges work with every OS.** The cart overlays $A000-$BFFF (8 KB) or
  $8000-$BFFF (16 KB) over RAM on any machine. Only XL/XE also reports it
  through TRIG3. RAM size is not modelled: the core always has 64 KB.
- **Warm reset = the Atari RESET key.** Raise an NMI with NMIST's RESET bit
  ($D40F bit 5) set; the OS then warm-starts and keeps memory. Check how the
  core's NMI path and `Antic_Read` expose NMIST, and add only what is
  missing. **Cold reset** = what F5 does today: `Atari_Reset()` plus the
  frontend's boot logic (OPTION hold when BASIC is off, delayed XEX load).
- **Disk/XEX and cartridge are independent slots.** Both are remembered in
  NVS and restored at power-on in the order OS, then cartridge (or the
  800-BASIC cartridge), then disk or XEX. A cold start keeps both. Loading an
  XEX does not eject the cartridge. With a cartridge inserted, the OPTION
  hold that drops built-in BASIC is unnecessary but harmless.
- **Setup changes apply by restarting the board.** OS, BASIC and PAL are
  saved to NVS and the board restarts (`esp_restart()`), like the CoCo's
  machine switch. That avoids a runtime re-init of video, pacing and audio.
  **PAL has never been tested on the board**, so the Setup row says so. If
  PAL turns out not to work, it may be left out of version 1, telling the
  user.
- **Drawing path follows measured memory.** About 41 KB of internal heap is
  free with the emulator running and the menu closed. Measure free memory
  with the OSD drawing; if FabGL's `Canvas` and font cost too much, draw text
  straight into the framebuffer with a small built-in font instead.

## Left to the builder

- The module split. The CoCo uses `supervisor.*`, `sv_menu.*`,
  `sv_filebrowser.*`, `sv_render.*`, `osd_canvas.*`; a smaller version of the
  same split is the expected shape.
- Icons (drawn in code, like the CoCo), fonts (FabGL's built-in fonts),
  exact colours within Atari blue/white, and tile sizes within 336x240.
- File-list limits, sorting, and scrolling. Long names are truncated
  sensibly.
- The NVS key names and the fallback order: NVS, then `autorun.txt`, then
  plain boot.
- How the core exposes cartridges: for example `Atari_InsertCartridge(a,
  data, size, type)` / `Atari_RemoveCartridge(a)`, with the image buffer
  owned by the frontend or by the core. Keep the core free of ESP32
  includes.
- How the paused emulator and the audio ring are handled on open and close.
- Whether F12 also works while a "not supported" or other popup is open.
