# Atari F12 supervisor menu: status and pending tests

Handoff for the next session. Branch `esp32-ttgo-vga32-port`.

- **Design:** `docs/superpowers/specs/2026-10-27-supervisor-design.md`
- **Plan:** `docs/superpowers/plans/2026-10-27-supervisor.md`
- **Work ledger** (git-ignored, on disk): `.superpowers/sdd/2026-10-27-supervisor/progress.md`

## Where things stand

| Plan task | State | Commits |
|---|---|---|
| 1. Core: 8/16 KB cartridges (`.rom`, `.car` types 1/2) | done, reviewed | d4fb74a |
| 2. Core: RESET-key warm reset | done, reviewed | 7edba35 |
| 3. OSD drawing layer + video pause | done, reviewed; **board check pending** | bd05a31 |
| 4. File browser (host-tested) | done, reviewed after 1 fix round | ceada9d, 067041f |
| 5. NVS settings + boot from settings, BASIC-as-cart on 800 OS | done, reviewed; **board check pending** | 3fcac20 |
| 6. Supervisor screens + wiring, `Atari_EjectDisk` | done, reviewed after 1 fix round; **board checklist pending** | 7e2b8d5, d47693e, 2453778 |
| 7. README section + final desktop re-verification | **not started** | |
| Final whole-branch review | **not started** | |

**Firmware 0.2.0** is built (`esp32/TTGO-VGA32-A800/build/out/`) and **was flashed to the board on 2026-10-27**. None of the board tests below have been run yet.

Task 6's implementer also fixed two older bugs:
- F5 cold reset cleared POKEY's `audio_rate`, which left the emulator silent and unpaced.
- The deferred XEX load opened its file after the SD card was unmounted. The XEX is now held in a RAM image and loaded with `Atari_LoadExecutableData`.

## Pending board tests

Setup:
- Card layout: ROMs in `/atari/roms/` (`ATARIXL.ROM`, `ATARIBAS.ROM`, `ATARIOSB.ROM`, `ATARIOSA.ROM`), media under `/atari/`.
- Put `Alien Garden.rom` (8 KB) and one or two ATRs under `/atari/`.
- For test 5, also put a small file renamed to `test.rom`.
- Watch the serial log at 115200 baud (`/dev/ttyACM0`).

| # | Test | Expected | Status |
|---|---|---|---|
| 1 | F12 on the BASIC screen; F12 again | Tile menu (Disks, Cart, Reset, Setup, About, Resume) over a paused, silent emulator; closing returns to BASIC with the cursor blinking | **pending** |
| 2 | Hold `A`, press F12, release, resume | No stuck or repeating `A` in BASIC | **pending** |
| 3 | Disks: enter a folder, back out with `..`, mount an ATR (YES) | The ATR boots; `EJECT D1:` row works | **pending** |
| 4 | Cart: insert `Alien Garden.rom` (YES) | The game runs; REMOVE CARTRIDGE brings BASIC back | **pending** |
| 5 | Cart: pick `test.rom` (wrong size) | `CANNOT LOAD FILE`; after Resume the previous state is unchanged | **pending** |
| 6 | Pull the card with the menu closed, then open Disks | `NO SD CARD`; the menu still works | **pending** |
| 7 | Reset: type `10 PRINT 1`, WARM, `LIST`; then COLD | WARM keeps the program; COLD clears it | **pending** |
| 8 | Setup: 800 OS-B + BASIC ON, APPLY AND RESTART; then power-cycle | BASIC `READY` on the 800 OS (BASIC loaded as a cart); setting kept after power-cycle; switch back to XL afterwards | **pending** |
| 9 | About | Version 0.2.0, machine, effective OS file, BASIC/cart/disk, internal and PSRAM free memory, last fps | **pending** |
| 10 | Menu closed, BASIC screen | Serial shows ~59 fps (it was 59.2 before the menu) | **pending** |

Also watch for these. The reviewers flagged them, and they can only be seen on the board:

| Item | What to look for | Status |
|---|---|---|
| a | **Holding F12:** keyboard auto-repeat may toggle the menu open and closed (fix: debounce ~300 ms) | **pending** |
| b | **Opening the menu in the first frames after boot** (XL, BASIC off): it may release the OPTION hold early, so BASIC comes up | **pending** |
| c | **Stack and NVS:** SD, FATFS and NVS calls now run inside the 8 KB core-0 emulation task stack. Watch for stack-overflow panics on mount or save, and for a VGA glitch while NVS saves (`Settings_Save` disables the flash cache) | **pending** |
| d | **Icons and layout:** icons and tile layout look right; long file names are shortened without overlapping rows; the reset arrow icon is the least certain | **pending** |
| e | **XL with built-in BASIC ON plus a game cart:** check whether the game starts or BASIC does. Today both are loaded and the cart wins the $A000 map | **pending** |

## How to flash

The SD card must be **out** of the slot while flashing, because the card holds GPIO2 high and causes "Wrong boot mode (0xa)". Put it back afterwards and press reset.

```
esp32/TTGO-VGA32-A800/build.sh                 # compile only
esp32/TTGO-VGA32-A800/build.sh /dev/ttyACM0    # compile and flash
arduino-cli upload --fqbn esp32:esp32:esp32wrover:PartitionScheme=huge_app \
  -p /dev/ttyACM0 --input-dir esp32/TTGO-VGA32-A800/build/out esp32/TTGO-VGA32-A800
```

## Desktop checks (must stay green)

The tooling was copied out of the old session's `/tmp` scratchpad into `T=.superpowers/sdd/2026-10-27-supervisor/tooling`. It holds the AltirraOS/BASIC ROMs, the 8-scenario scripts, the baseline, and the POKEY mutation runner.

```
T=.superpowers/sdd/2026-10-27-supervisor/tooling
for v in full line; do
  opt=OFF; [ $v = line ] && opt=ON
  cmake -S . -B $T/build-$v -DA800_BUILD_SDL=OFF -DCMAKE_BUILD_TYPE=Release -DA800_LINE_BUFFER=$opt
  cmake --build $T/build-$v -j4
  $T/scenarios.sh $T/build-$v/a800-validate | diff - $T/base.txt          # must print nothing
  $T/scenarios-defer.sh $T/build-$v/a800-validate | diff - $T/base.txt    # must print nothing
done
cmake -S . -B $T/build-dbg -DA800_BUILD_SDL=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build $T/build-dbg -j4 && (cd $T/build-dbg && ctest --output-on-failure)   # Debug: asserts on
bash esp32/TTGO-VGA32-A800/tests/host_test.sh                                       # browser + settings
python3 -I $T/mutate.py $PWD $T/mut                                                  # every line "caught"
```

If the ROMs are ever lost, re-extract them with `python3 -I $T/ext.py <in.c> <out.ROM>`. The sources are `/home/pi/proyectos/atari800/atari800/src/roms/altirraos_xl.c` → `ATARIXL.ROM`, `altirra_basic.c` → `ATARIBAS.ROM` and `altirraos_800.c` → `ATARIOSB.ROM`.

## Next steps

1. Run the board tests above and mark each one pass or fail here.
2. Fix any failures. Likely candidates are the F12 debounce (a) and the OPTION hold (b).
3. Plan Task 7: write the README section, then re-run every desktop check.
4. Final whole-branch review, on the most capable model. Point it at the deferred minor findings below and the decision list.
5. Merge or PR decision, with the user.

## Deferred minor findings (for the final review to triage)

- **Cartridge loading:**
  - `AtariIo_LoadCartridge` reads the whole file before checking its size.
  - A `.car` with trailing bytes is accepted.
  - The cart tests have small gaps: TRIG3 is set by hand, remove is tested on XL only, and temp-file removal is unchecked.
- **Video and OSD:**
  - `Video_Pause` spins with no timeout.
  - The core-0-only contract is only documented.
  - `osd.cpp` duplicates the `row[x^2]` macro.
- **Browser:**
  - Two ~66 KB entry arrays coexist while a listing is swapped (PSRAM).
  - `Browser_FullPath` truncates silently into a small caller buffer.
- **Boot:**
  - An NVS disk that exists but fails to mount doesn't fall back to autorun.
  - A `Settings_Load` failure is silent on serial.
- **Menu:**
  - The last folder is only remembered on ESC.
  - ENTER on an empty EJECT row is silent.
  - A card pulled while the menu is open shows `CANNOT READ /ATARI` instead of `NO SD CARD`.
  - A small XEX is held twice in the internal heap.
- **Commit trailer:** commit 7edba35 names "Claude Haiku 5.5" in its co-author trailer.

## Decisions taken without asking (review these)

1. **Board checks:** implementers didn't run them. They were batched for one session with the user, because each flash needs the card removed.
2. **Branch:** no separate worktree; the work is on the existing feature branch.
3. **Icon background:** `Osd_Icon` gained a background-colour parameter, so icon holes match the selected tile.
4. **Browser root:** `Browser` got a `root` field (`/sd/atari`). `..` only appears below it, and Enter never climbs above it.
5. **Browser names:** entries keep full 255-byte names, and the menu shortens them for display. A `capped` flag replaces the plan's 63-byte truncation. Cost: ~66 KB PSRAM per listing.
6. **Browser cap:** over 256 files, the first 256 that `readdir` returns are kept, and the cap is flagged.
7. **Boot sequence:** `boot_machine()` runs once at power-on and ends with the only `Video_Init`. Menu resets use the F5 path.
8. **Autorun path:** `autorun.txt` media is passed as a full `/sd/...` path.
9. **Boot fallbacks:** a missing saved cart on an 800 OS falls back to the BASIC cart, and About shows the OS that actually booted.
10. **Eject:** added a core `Atari_EjectDisk`.
11. **Saving:** every save stores both the running disk and the running cart.
12. **Silence fast path** (earlier session): it only triggers for silence that was never preceded by sound, because a decayed filter sticks at denormals.
