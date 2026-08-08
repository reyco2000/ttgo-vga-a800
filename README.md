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
  double-width character modes, plus approximate bitmap-mode rendering
- scanline DLI scheduling with per-line GTIA color capture
- GTIA color registers, console/trigger input, and an Atari-inspired palette
- basic player/missile DMA rendering with positions, sizes, and colors
- PIA joystick and PORTB registers
- POKEY keyboard, random register, basic timer IRQs, and four-channel square
  wave audio
- read-only ATR parsing, sector access, and an XL OS SIOV acceleration for
  disk status and reads
- segmented XEX loading with ordered INITAD execution and RUNAD handoff after
  OS startup
- SDL2 video, audio, keyboard, joystick-key, and console-key frontend

Important current limits: ANTIC/GTIA are scanline approximations rather than
cycle-exact; player/missile graphics, collisions, scrolling, serial SIO command
  transport at the POKEY bit level, cartridges, save states, and undocumented
CPU instructions are not implemented. The SIOV acceleration lets
the stock XL OS boot compatible ATR images without bit-level serial emulation.

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
| `AtariIo.c` | ROM files and ATR sector layout |
| `Atari.c` | machine wiring and memory map |
| `A800.c` | SDL2 application |

## Provenance

The code in this directory was written for this simplified implementation; it
does not copy A8E source. Atari800 (GPL-2.0-or-later) was used as an
architectural and behavioral reference, especially its CPU, memory, ANTIC,
GTIA, POKEY, and PIA module boundaries. See <https://atari800.github.io/>.
