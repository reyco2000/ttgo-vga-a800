# a800

`a800` is a small, readable Atari 800 XL (NTSC or PAL) emulator in C. Its flat source
layout follows A8E (`6502.c`, `Antic.c`, `Gtia.c`, `Pokey.c`, `Pia.c`, and
`AtariIo.c`), while the implementation is new code informed by Atari800's
hardware organization and the Atari register model.

This is an early simplified emulator, not a replacement for Atari800. It is
useful as a compact core to read and extend.

## Implemented

- NMOS 6502 official instruction set, decimal mode, interrupts, and indirect
  JMP page-wrap behavior (unknown/illegal opcodes currently behave as NOPs)
- 64 KiB XL address space, OS/BASIC/self-test overlays, and PORTB banking
- selectable NTSC (default: 262 lines, 1.790 MHz, 59.92 Hz) or PAL (312
  lines, 1.773 MHz, 49.86 Hz) timing, WSYNC, VCOUNT, GTIA PAL identification,
  and VBI NMI
- ANTIC display-list traversal, including monochrome, multicolor, and
  double-width character modes, CHACTL, practical HSCROL/VSCROL, and
  approximate bitmap-mode rendering
- scanline DLI scheduling with per-line ANTIC/GTIA register and PMG DMA capture
- GTIA color registers, modes 9/10/11 for ANTIC mode F, console/trigger input,
  and an Atari-inspired palette
- basic player/missile DMA or direct-GRAF rendering with positions, sizes,
  VDELAY, fifth-player and multicolor priority mixing, sticky collision
  latches, HITCLR, and latched joystick triggers
- PIA joystick and PORTB registers
- POKEY keyboard, random register, basic timer IRQs, and four-channel square
  wave audio
- read-only ATR parsing, sector access, and an XL OS SIOV acceleration for
  disk status and reads
- segmented XEX loading with ordered INITAD execution and RUNAD handoff after
  OS startup
- SDL2 video, audio, keyboard, joystick-key, and console-key frontend

Important current limits: ANTIC/GTIA are scanline approximations rather than
cycle-exact; GTIA modes on character modes, precise scrolling fetch windows,
serial SIO command transport at the POKEY bit level, cartridges, save states,
and undocumented CPU instructions are not implemented. The SIOV acceleration lets
the stock XL OS boot compatible ATR images without bit-level serial emulation.
Display-list state, playfield data, colors, character-base, player/missile
data, and collisions now advance one scanline at a time. Register changes
within a scanline are still approximated at its ending boundary.

### Reference audit priorities

A focused comparison with Atari800, A8E, and the local hardware reference
identified these next compatibility improvements while retaining the flat,
small implementation:

1. Refine HSCROL fetch promotion/clipping and VSCROL entry/exit row counts,
   then extend GTIA modes 9, 10, and 11 from ANTIC mode F to modes 2/3.
2. Improve CPU cycle counts (especially page crossings and DMA steals) and add
   the stable undocumented opcodes used by software.
3. Refine POKEY AUDCTL, joined timers, noise, and timer start/IRQ timing.
4. Model same-scanline player/missile repositioning and shift-register
   retrigger behavior only where software compatibility demonstrates a need.

Lower-priority subsystems such as cartridges, save states, and bit-level SIO
should remain separate additions rather than enlarging the core preemptively.

## Build

Requirements: a C99 compiler, CMake 3.16+, and SDL2 development files.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

For a headless core-only build, which needs no SDL:

```sh
cmake -S . -B build-core -DA800_BUILD_SDL=OFF
cmake --build build-core
ctest --test-dir build-core --output-on-failure
```

## Run

Provide exact-size Atari XL ROM dumps (not included):

- `ATARIXL.ROM` — 16 KiB
- `ATARIBAS.ROM` — 8 KiB

```sh
./build/a800
./build/a800 --pal            # select PAL; NTSC is the default
./build/a800 --ntsc           # explicitly select NTSC
./build/a800 -b
./build/a800 game.atr
./build/a800 program.xex
./build/a800 --os path/to/ATARIXL.ROM --basic path/to/ATARIBAS.ROM
```

Without `-b`, BASIC is disabled for normal boot. XEX startup performs the XL
OS initialization needed by programs, transfers control while the CPU is still
executing OS code, disables the BASIC bank at handoff, and temporarily holds
OPTION for executables that invoke the XL warm-start path. During that launch
window, a warm start with a RAM `DOSINI` hook transfers to the hook directly;
this is a compact substitute for Atari800's virtual boot-device loader. BASIC
code is not executed and `READY` is not displayed while launching a XEX.

Controls: arrows are joystick directions, Left/Right Alt is fire, F2/F3/F4
are OPTION/SELECT/START, F5 resets, and F8 is BREAK. Normal letter, number,
punctuation, Return, Space, Backspace, Escape, Tab, Shift, and Control input is
mapped to the Atari keyboard matrix and POKEY registers.

## Source map

| File | Responsibility |
| --- | --- |
| `6502.c` | CPU execution and interrupts |
| `Antic.c` | NTSC/PAL frame timing and display-list renderer |
| `Gtia.c` | colors and console/trigger registers |
| `Pokey.c` | keyboard, timers, random source, and audio |
| `Pia.c` | joystick and XL memory-control ports |
| `AtariIo.c` | ROM, ATR/SIOV, and segmented XEX loading |
| `Atari.c` | machine wiring and memory map |
| `A800.c` | SDL2 application |

The hardware-named files are intentionally retained even when small. Merging
`Gtia.c` into `Antic.c`, or folding `Pia.c`/`Pokey.c` into `Atari.c`, would mix
register-device behavior with the scanline scheduler and memory bus. The
current boundaries match the requested A8E-style layout and leave room for the
remaining behavior without requiring additional source files.

## Validation utility

The build also produces `a800-validate`, a headless replacement for the small
one-off boot and framebuffer probes used during development. It reports CPU,
banking, ANTIC, GTIA, POKEY, and framebuffer state and can save the final frame
as a portable pixmap:

```sh
./build/a800-validate --basic --frames 180
./build/a800-validate --frames 300 program.xex
./build/a800-validate --frames 300 --output frame.ppm disk.atr
./build/a800-validate --pal --frames 180
```

Use `--os` and `--basic-rom` to select ROM files. The utility returns failure
for invalid arguments, ROM/media errors, or a final frame with playfield DMA
disabled. `--xex-frame N` overrides the default frame-76 XEX handoff for boot
sequencing investigations.

## Provenance

The code in this directory was written for this simplified implementation; it
does not copy A8E source. Atari800 (GPL-2.0-or-later) was used as an
architectural and behavioral reference, especially its CPU, memory, ANTIC,
GTIA, POKEY, and PIA module boundaries. See <https://atari800.github.io/>.
