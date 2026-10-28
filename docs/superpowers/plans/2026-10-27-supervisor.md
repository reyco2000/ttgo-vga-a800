# TTGO Supervisor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** An F12 on-screen supervisor for the TTGO VGA32 Atari emulator. It
mounts ATR/XEX media, inserts standard cartridges, resets, sets OS/BASIC/PAL
(kept in NVS), and shows About.

**Architecture:**
- **Core (shared, desktop-testable):** gains standard 8/16 KB cartridges
  (the loader in `AtariIo.c`, the mapping in `Atari.c`'s page decoder) and a
  RESET-key warm reset.
- **Frontend:** gains an OSD layer that draws into the VGA framebuffer with
  the loaded OS ROM's own 8x8 font and the Atari palette. It also gains a
  file browser on POSIX `dirent`, NVS settings, and a supervisor state
  machine.
- **Pausing and boot:** `run_frame()` pauses emulation while the supervisor
  is open. Setup changes restart the board.

**Tech Stack:**
- C99 core, built with CMake on desktop.
- Arduino C++ frontend: arduino-cli, `esp32:esp32@2.0.17`, FabGL 1.0.9, and
  `Preferences` for NVS.

**Spec:** `docs/superpowers/specs/2026-10-27-supervisor-design.md`

## Global Constraints

- Build: `esp32/TTGO-VGA32-A800/build.sh`; FQBN `esp32:esp32:esp32wrover:PartitionScheme=huge_app`; FabGL does not build on core 3.x.
- C and C++ see identical core flags (`CORE_FLAGS` in `build.sh`), because the `Atari` struct layout depends on them.
- Flashing requires the SD card out of the slot (GPIO2 strap: "Wrong boot mode (0xa)").
- IRAM is nearly full (~126.9 KB of ~129.7 KB). Supervisor code is never marked `IRAM_ATTR`/`A800_HOT`.
- Any file that stores to PSRAM defines `A800_PSRAM_STORES` before its includes; `A800_NO_PSRAM_MEMW` strips `memw` everywhere else.
- Memory-map changes call `Atari_InvalidateMemoryMap()`.
- `Atari_Init()` clears `line_sink`, `line_defer` and `line_sink_context`; the frontend re-installs them via `Video_Init`.
- With no cartridge inserted, every `a800-validate` scenario hash must stay identical to the baseline (full-frame, `A800_LINE_BUFFER`, and `--defer`), and all `ctest` tests pass.
- The core includes no ESP32 headers.
- Closed-menu cost per frame ≈ 0: the board must stay at ~59 fps (target 59.92).
- UI copy: confirmation popups default to **No**; unsupported files show `CARTRIDGE TYPE NOT SUPPORTED` / `CANNOT LOAD FILE`.
- Card layout: ROMs in `/sd/atari/roms/` (`ATARIXL.ROM`, `ATARIOSB.ROM`, `ATARIOSA.ROM`, `ATARIBAS.ROM`); media under `/sd/atari/`.
- Hotkey **F12** opens and closes. ESC goes back; arrows and ENTER navigate.

## Review Focus

1. **A folder with hundreds of files, very long names, or names with non-ASCII bytes:** the list caps, scrolls, truncates, and never crashes or overruns. Tests: Task 4 `browser_caps_entries`, `browser_truncates_names`.
2. **The SD card is missing or unreadable when Disks/Cart opens:** a `NO SD CARD` message appears and the menu keeps working. Test: Task 4 `browser_missing_dir`, plus the Task 6 hardware check.
3. **A bad file is picked** (a `.rom` of the wrong size, an unsupported `.car` type, a truncated `.car`): a popup appears, and the running machine is untouched (old cart still in, map intact). Tests: Task 1 `cart_rejects_*`, `cart_failed_load_keeps_old`.
4. **NVS names a file that was deleted from the card:** boot ignores it and falls back (autorun, then plain boot) without hanging. Test: Task 5 `choose_media_*`.
5. **A key is held when F12 opens the menu,** or keys are pressed in the menu: the Atari sees no stuck key after resume, and menu keys never reach the Atari. Hardware check in Task 6 (needs real PS/2 timing).

---

## File Structure

| File | Responsibility |
|---|---|
| `Atari.h`, `Atari.c` | cartridge fields; cart pages in `read_page`/`write_page`; `Atari_WarmReset` |
| `AtariIo.c`, `AtariIo.h` | `AtariIo_LoadCartridge` (raw `.rom`, `.car` types 1/2), `Atari_RemoveCartridge` |
| `State.c` | refuse to save while a cartridge is inserted; clear cart fields on load |
| `tests/core_test.c` | cartridge and warm-reset tests |
| `esp32/.../src/hal/video.cpp`, `hal.h` | `Video_Pause()`/`Video_Resume()`, `Video_Row()`, `Video_Colour()` |
| `esp32/.../src/ui/osd.{h,cpp}` | primitives: rect, frame, text in the OS-ROM font, tile icons |
| `esp32/.../src/ui/browser.{h,cpp}` | directory listing, filter, sort, cap, truncation (host-testable) |
| `esp32/.../src/ui/settings.{h,cpp}` | NVS load/save; `Settings_ChooseMedia` pure decision (host-testable) |
| `esp32/.../src/ui/supervisor.{h,cpp}` | state machine, screens, actions |
| `esp32/.../tests/host_test.sh`, `tests/*_test.cpp` | g++ host tests for browser and settings logic |
| `esp32/.../TTGO-VGA32-A800.ino` | `boot_machine()` from settings; pause while open; F12 routing |
| `esp32/.../src/hal/keyboard.cpp` | F12 action; key queue routed to supervisor while open; release-all |
| `README.md` | supervisor section |

---

### Task 1: Core cartridges

**Files:**
- Modify: `Atari.h` (struct fields and prototypes), `Atari.c` (`read_page`, `write_page`, `Atari_Destroy`), `AtariIo.c`/`AtariIo.h`, `State.c`
- Test: `tests/core_test.c`

**Interfaces:**
- Produces:
  - `int AtariIo_LoadCartridge(Atari *a, const char *path);` returns 0 = inserted, -1 = cannot read or wrong size, -2 = unsupported `.car` type. On failure the previous cartridge stays inserted and the map is unchanged.
  - `void Atari_RemoveCartridge(Atari *a);`
  - Struct fields `uint8_t *cart; uint16_t cart_size;` (0, 8192 or 16384). The core owns `cart`: it is malloc'd, and freed by remove/destroy.

- [ ] **Step 1: Write the failing tests** in `tests/core_test.c` (helpers write temp files the way the existing `core_test_os.xex` test does):
  - `cart_8k_maps_a000`: insert an 8192-byte image with byte[0]=0x11 and byte[0x1fff]=0x22 → `Atari_Read(0xa000)==0x11`, `Atari_Read(0xbfff)==0x22`, and `Atari_Read(0x9fff)` returns RAM.
  - `cart_16k_maps_8000`: 16384 bytes → `Atari_Read(0x8000)` is byte[0], `Atari_Read(0xbfff)` is byte[0x3fff].
  - `cart_writes_ignored`: `Atari_Write(0xa000, 0x55)` leaves the read value and `ram[0xa000]` unchanged.
  - `cart_beats_basic`: XL OS with BASIC enabled (`pia[1]` bit1 = 0) → $A000 reads the cart, not `basic[0]`.
  - `cart_remove_restores`: after `Atari_RemoveCartridge`, $A000 reads BASIC (XL with BASIC on) or RAM again. `cart == NULL`, `cart_size == 0`.
  - `cart_car_types`: a `.car` with header `"CART"`, big-endian type 1 + 8192 bytes → inserted at $A000; type 2 + 16384 → $8000.
  - `cart_rejects_unsupported`: `.car` type 12 → -2; a `.rom` of 4000 bytes → -1; a `.car` whose data is shorter than its type → -1.
  - `cart_failed_load_keeps_old`: insert A, then a failing load → A still mapped.
  - `cart_survives_reset`: `Atari_Reset` keeps the cart mapped.
  - `cart_trig3_xl`: XL with a cart → `Atari_Read(0xd013) & 1 == 1`.

- [ ] **Step 2: Run them to verify they fail**
  Run: `cmake --build <build-dbg> && (cd <build-dbg> && ctest --output-on-failure -R core_test)`
  Expected: FAIL (undefined `AtariIo_LoadCartridge`).

- [ ] **Step 3: Implement.**
  - Cart pages ($C000 − `cart_size` up to $BFFF) come first in `read_page()`, ahead of BASIC and RAM. `write_page()` returns NULL for them.
  - Insert and remove call `Atari_InvalidateMemoryMap()`.
  - `AtariIo.c` already defines `A800_PSRAM_STORES`, and it owns the buffer.
  - `.car` header layout: bytes 0-3 `"CART"`, bytes 4-7 type (big-endian), bytes 8-11 checksum (ignored), bytes 12-15 unused, then the data.
  - TRIG3 needs no change if `cart_trig3_xl` already passes (`trigger[3]` is 1). If it fails, return 1 for TRIG3 while `cart` is set on XL.
  - `State_Save` returns -1 when `cart` is set. `State_Load` sets `cart = NULL, cart_size = 0` before `*a = image` is used (keep the existing sink-preserving pattern).

- [ ] **Step 4: Run tests and exactness**
  - Run `ctest`: all pass.
  - Run the eight scenarios through `a800-validate` in the full-frame and line builds, with and without `--defer`. All must be identical to the baseline output.

- [ ] **Step 5: Commit** (`Add standard 8/16 KB cartridges to the core`).

### Task 2: Core warm reset

**Files:** Modify `Atari.c`, `Atari.h`; Test `tests/core_test.c`

**Interfaces:**
- Produces: `void Atari_WarmReset(Atari *a);`, the RESET key. It sets NMIST ($D40F) bit 5, clears bits 6-7, and raises `nmi`. It ignores NMIEN, as the hardware does. RAM, the cart and media are untouched.

- [ ] **Step 1: Failing test** `warm_reset_raises_reset_nmi`:
  - Set `ram[0x600]=0x77`, then call `Atari_WarmReset`.
  - Assert `a.nmi == 1`, `(Atari_Read(0xd40f) & 0xe0) == 0x20`, and `ram[0x600] == 0x77`.
  - After one `Cpu_Step`, `pc` equals the $FFFA vector.
- [ ] **Step 2:** Run it; expect an undefined-symbol failure.
- [ ] **Step 3:** Implement it in `Atari.c` (two assignments).
- [ ] **Step 4:** `ctest` all pass; scenarios identical.
- [ ] **Step 5: Commit** (`Add a RESET-key warm reset`).

### Task 3: OSD drawing layer and video pause

**Files:**
- Create: `esp32/TTGO-VGA32-A800/src/ui/osd.h`, `osd.cpp`
- Modify: `src/hal/video.cpp`, `src/hal/hal.h`

**Interfaces:**
- Produces, in `hal.h`:
  - `void Video_Pause(void);` blocks until the core-1 queue is empty (`drawn == queued`) and the drawer is idle.
  - `void Video_Resume(void);`
  - `uint8_t *Video_Row(int y);`
  - `uint8_t Video_Colour(uint8_t atari_colour);` returns the raw FabGL byte from `lut`.
- Produces, in `osd.h` (coordinates in the 336x240 screen, colours as Atari palette indices):
  - `void Osd_Begin(const Atari *a);` remembers the OS font: the charset at `a->os + 0x2000` (`$E000`), the same offset for XL and 800 images.
  - `void Osd_Rect(int x, int y, int w, int h, uint8_t colour);`
  - `void Osd_Frame(int x, int y, int w, int h, uint8_t colour);`
  - `void Osd_Text(int x, int y, const char *s, uint8_t fg, uint8_t bg);` uses the 8x8 OS font. ASCII is converted to ANTIC internal code: 0x20-0x5F → c−0x20; 0x60-0x7F → c; others → `?`.
  - `int Osd_TextCols(void);` returns 42.
  - `void Osd_Icon(int x, int y, OsdIcon icon, uint8_t fg);` with `enum OsdIcon { ICON_DISK, ICON_CART, ICON_RESET, ICON_SETUP, ICON_ABOUT, ICON_RESUME };`
- Palette constants in `osd.h`:
  - `OSD_BG = 0x94`, the Atari editor blue.
  - `OSD_TILE = 0x86`.
  - `OSD_FG = 0x0f`.
  - `OSD_SEL_BG = 0x0f` and `OSD_SEL_FG = 0x94`, the inverted selection.

- [ ] **Step 1:** Implement `Video_Pause`/`Video_Resume`/`Video_Row`/`Video_Colour` in `video.cpp` and the OSD primitives in `osd.cpp`. Draw pixels with `VGA_PIXELINROW(row, x) = Video_Colour(c)`. Icons are drawn in code from rects and lines, about 32x24 each.
- [ ] **Step 2: Temporary board check.** Under `#if OSD_SELFTEST` (config.h, default 0), `setup()` draws a frame, the text `A800 SUPERVISOR` and the six icons for 3 s.
  - Build with it set to 1 and flash (card out).
  - Expect the Atari font, white on blue, legible, and no stray emulator lines.
  - Log `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` before and after. The OSD adds no heap use.
- [ ] **Step 3:** Set `OSD_SELFTEST` back to 0. Build passes.
- [ ] **Step 4: Commit** (`Add the OSD drawing layer`).

### Task 4: File browser (host-tested)

**Files:**
- Create: `esp32/TTGO-VGA32-A800/src/ui/browser.h`, `browser.cpp`, `esp32/TTGO-VGA32-A800/tests/browser_test.cpp`, `esp32/TTGO-VGA32-A800/tests/host_test.sh`

**Interfaces:**
- Produces:
  - `struct BrowserEntry { char name[64]; bool dir; };`
  - `struct Browser { BrowserEntry *entries; int count; bool truncated; char path[128]; };`
  - `int Browser_Open(Browser *b, const char *path, const char *const *exts);` returns 0 or -1 if the directory can't be opened. `exts` is NULL-terminated, lower case (e.g. `{".atr", ".xex", NULL}`).
  - `void Browser_Close(Browser *b);`
  - `int Browser_Enter(Browser *b, int index, const char *const *exts);` descends into a directory, or into `..`.
  - `void Browser_FullPath(const Browser *b, int index, char *out, size_t n);`
  - `BROWSER_MAX = 256` entries, allocated with `malloc` (lands in PSRAM). The file defines `A800_PSRAM_STORES`.
- Rules:
  - `..` comes first when below the root `/sd/atari`.
  - Then directories, then files; each group case-insensitively sorted.
  - Hidden names (leading `.`) are skipped.
  - Names longer than 63 bytes are truncated, with `truncated` set.
  - Non-ASCII bytes are kept in `name`; `Osd_Text` renders them as `?`.

- [ ] **Step 1: Write failing host tests** in `browser_test.cpp`. Each builds a temp directory tree with `mkdir`/`fopen`:
  - `browser_filters_and_sorts`: `b.ATR, a.atr, z.xex, readme.txt, Games/` → `[Games/, a.atr, b.ATR, z.xex]`.
  - `browser_dotdot_below_root`: opening `root/Games` lists `..` first; `Browser_Enter(..)` returns to `root`.
  - `browser_caps_entries`: 300 `.atr` files → `count == 256`, no crash.
  - `browser_truncates_names`: a 100-char name is listed truncated to 63 bytes and NUL-terminated.
  - `browser_missing_dir`: a nonexistent path → -1, `count == 0`.
- [ ] **Step 2:** `host_test.sh` compiles with `g++ -std=c++17 -Wall -Werror src/ui/browser.cpp tests/browser_test.cpp -o /tmp/...` and runs it. Expect a FAIL or link error first.
- [ ] **Step 3:** Implement with `opendir`/`readdir`/`stat`. These work on host and on ESP32 VFS (`/sd`).
- [ ] **Step 4:** `tests/host_test.sh` passes, and `build.sh` compiles.
- [ ] **Step 5: Commit** (`Add the supervisor file browser`).

### Task 5: Settings, boot choices, and the reworked boot

**Files:**
- Create: `src/ui/settings.h`, `settings.cpp`, `tests/settings_test.cpp`
- Modify: `TTGO-VGA32-A800.ino` (replace the ROM/media part of `setup()` with `boot_machine()`), `config.h` (drop `USE_BASIC`/`USE_PAL` as runtime sources; keep them as first-boot defaults), `tests/host_test.sh`

**Interfaces:**
- Produces:
  - `enum OsChoice : uint8_t { OS_XL = 0, OS_800B = 1, OS_800A = 2 };`
  - `struct Settings { OsChoice os; bool basic; bool pal; char disk[128]; char cart[128]; };`, where `disk` and `cart` are full `/sd/...` paths or `""`.
  - `void Settings_Load(Settings *s);`: NVS namespace `"a800"`, keys `os`, `basic`, `pal`, `disk`, `cart`. Missing or invalid values fall back to the `config.h` defaults.
  - `void Settings_Save(const Settings *s);`
  - `const char *Settings_OsPath(OsChoice os);` returns `OS_ROM`, `ROM_DIR "/ATARIOSB.ROM"` or `ROM_DIR "/ATARIOSA.ROM"`.
  - Pure and host-tested: `void Settings_ChooseMedia(const Settings *s, bool (*exists)(const char *), const char *autorun, char *media, size_t n);`. The result is `s->disk` if it exists, else `autorun` if non-empty and it exists, else `""`.
  - In the `.ino`: `bool boot_machine(const Settings *s);` applies, in order:
    1. `Atari_Init(Settings_OsPath(os), XL && basic ? BASIC_ROM : NULL)`.
    2. Video standard.
    3. The cart: `s->cart`; or, with a non-XL OS and `basic`, `BASIC_ROM` as a cart. A game cart wins.
    4. The media: XEX deferred as today; ATR via `Atari_LoadDisk`.
    5. `Video_Init` (re-installs the hooks).
  - The OPTION-hold rule stays: XL with BASIC off.
- Prerequisite: the SD card must be mounted around `boot_machine()` (`Storage_Init`/`Storage_End`, as now).

- [ ] **Step 1: Failing host tests** `choose_media_prefers_nvs`, `choose_media_skips_missing_nvs` (NVS disk missing → autorun used), `choose_media_none` (neither exists → `""`), `os_path_values`.
- [ ] **Step 2:** Run `host_test.sh` and expect FAIL. Implement; it passes.
- [ ] **Step 3:** Rework `setup()` to `Settings_Load` → mount SD → `boot_machine()` → `Storage_End()`. Keep the `ROM load failed` halt messages.
- [ ] **Step 4: Board check (card out to flash).**
  - With empty NVS, boot is unchanged: XL, BASIC `READY`, ~59 fps on serial.
  - Temporarily set the NVS default to `OS_800B` + BASIC: BASIC `READY` appears with the 800 OS (BASIC loaded as a cart).
  - Restore the defaults.
- [ ] **Step 5: Commit** (`Boot from saved settings; BASIC as a cartridge on the 800 OS`).

### Task 6: Supervisor screens and wiring

**Files:**
- Create: `src/ui/supervisor.h`, `supervisor.cpp`
- Modify: `TTGO-VGA32-A800.ino`, `src/hal/keyboard.cpp`, `src/hal/hal.h`, `config.h` (`FIRMWARE_VERSION "0.2.0"`)

**Interfaces:**
- Consumes: Tasks 1-5 (`AtariIo_LoadCartridge`, `Atari_RemoveCartridge`, `Atari_WarmReset`, `Osd_*`, `Video_Pause`/`Resume`, `Browser_*`, `Settings_*`, `boot_machine`).
- Produces:
  - `void Supervisor_Open(Atari *a, Settings *s);`
  - `bool Supervisor_Active(void);`
  - `void Supervisor_Key(int vk, uint8_t ascii, bool down);`, where `vk` is a FabGL `VirtualKey`.
  - `void Supervisor_Update(void);` redraws when dirty and applies pending actions.
  - `KEY_ACTION_SUPERVISOR = 2`, returned by `Keyboard_Poll` on F12 down.
  - While active, `Keyboard_Poll` forwards every key event to `Supervisor_Key`, not to the Atari.
  - On open, `Keyboard_ReleaseAll(Atari *a)` (new in `keyboard.cpp`) sends key-up for the held key, clears the joystick bits and trigger, and restores `console = 7`.
- States:
  - `MAIN` is the 3x2 tile grid: Disks, Cart, Reset, Setup, About, Resume.
  - `DISKS` is a browser over `/sd/atari`, with `.atr`/`.xex`. Its first row is `EJECT D1: <current>`, then the folder listing.
  - `CART` is a browser with `.rom`/`.car`. Its first row is `REMOVE CARTRIDGE`.
  - `RESET` is a popup with rows `NO`, `WARM`, `COLD`.
  - `SETUP` is a list:
    - `OS: XL | 800 OS-B | 800 OS-A`
    - `BASIC: ON | OFF`
    - `VIDEO: NTSC | PAL (UNTESTED)`
    - `APPLY AND RESTART`
  - `ABOUT` shows version, machine name, OS file, BASIC/cart/disk, internal and PSRAM free memory, and the last fps.
  - `CONFIRM` is a generic `NO`/`YES` popup, starting on `NO`.
  - `MESSAGE` is a one-line popup dismissed by any key.
- Actions:
  - Mount ATR → `CONFIRM "REBOOT NOW?"`; YES → `Atari_LoadDisk`, cold reset, `Settings_Save`.
  - XEX → the same, with the deferred XEX boot.
  - Cart insert/remove → `CONFIRM` → cold reset, save.
  - Warm → `Atari_WarmReset`. Cold → today's F5 path.
  - Setup apply → `Settings_Save` + `esp_restart()`.
  - Resume, ESC from `MAIN`, or F12 anywhere → close.
- Opening calls `Video_Pause()` and `Storage_Init()`; when that fails, Disks/Cart show `MESSAGE "NO SD CARD"`.
- Closing calls `Storage_End()` and `Video_Resume()`, resets the audio pacing state, and the emulator repaints on the next frame.
- In `run_frame()`: when `Supervisor_Active()`, call `Supervisor_Update()` and `vTaskDelay(1)`, and return without emulating.

- [ ] **Step 1:** Implement the state machine and screens per the above, drawing only through `Osd_*`.
- [ ] **Step 2:** `build.sh` compiles with no warnings; IRAM size unchanged (±0.5 KB).
- [ ] **Step 3: Board checklist (card out to flash, card back in).** Each item passes:
  1. F12 opens the tile menu over a paused, silent emulator, and F12 closes it. BASIC is intact and the cursor blinks again.
  2. Hold `A` and press F12 → after resume no repeating `A` appears.
  3. Disks: browse into a folder and back with `..`. Mount an ATR → `YES` → it boots. Eject works.
  4. Cart: insert `Alien Garden.rom` → `YES` → the game runs. Remove the cartridge → BASIC returns.
  5. A wrong-size `.rom` → `CANNOT LOAD FILE`, and the previous state is unchanged after Resume.
  6. Card removed while the menu is closed, then Disks opened → `NO SD CARD`; the menu still works.
  7. Reset: WARM keeps the BASIC program in memory (type `10 PRINT 1`, warm reset, `LIST` shows it); COLD clears it.
  8. Setup: switch to 800 OS-B with BASIC on → restart → BASIC `READY`. Power-cycle → still OS-B. Switch back to XL.
  9. About shows the values listed above.
  10. Closed-menu performance: serial shows ~59 fps on the BASIC screen.
- [ ] **Step 4: Commit** (`Add the F12 supervisor menu`).

### Task 7: Documentation and final verification

**Files:** Modify `README.md`, plus the memory notes if anything there changed.

- [ ] **Step 1:** Add a README section "TTGO VGA32 supervisor" covering:
  - F12, the tiles, and the card layout (`/atari/roms`, `/atari`).
  - The supported cartridges (8/16 KB `.rom`, `.car` types 1/2).
  - The flashing note (card out), and the build/flash commands from `build.sh`.
- [ ] **Step 2:** Desktop full verification:
  - `ctest` all pass.
  - The eight scenarios ×4 builds identical.
  - `host_test.sh` passes.
  - `pokey_reference_test` passes.
- [ ] **Step 3: Commit** (`Document the TTGO supervisor`).
