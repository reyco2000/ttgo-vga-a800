# a800

`a800` is a small, readable Atari 800 XL (NTSC or PAL) emulator in C. Its flat
source layout follows A8E (`6502.c`, `Antic.c`, `Gtia.c`, `Pokey.c`, `Pia.c`,
and `AtariIo.c`), while the implementation is new code informed by Atari800's
hardware organization and the Atari register model.

This is an early simplified emulator, not a replacement for Atari800. It is
useful as a compact core to read and extend.

## Implemented

- NMOS 6502 official instruction set, decimal mode, interrupts, indirect JMP
  page-wrap behavior, stable undocumented read/modify/write and load/store
  families, and undocumented NOP encodings
- 64 KiB XL address space, OS/BASIC/self-test overlays, and PORTB banking
- selectable NTSC (default: 262 lines, 1.790 MHz, 59.92 Hz) or PAL (312
  lines, 1.773 MHz, 49.86 Hz) timing, WSYNC, VCOUNT, GTIA PAL identification,
  VBI NMI, practical display-list/playfield/character/PMG DMA stealing, and
  cross-scanline accounting for instruction-boundary timing overruns
- ANTIC display-list traversal, including monochrome, multicolor, and
  double-width character modes, CHACTL, practical HSCROL/VSCROL, and
  approximate bitmap-mode rendering
- scanline DLI scheduling with per-line ANTIC/GTIA register and PMG DMA capture
- GTIA color registers, modes 9/10/11 for ANTIC mode F, console/trigger input,
  and an Atari-inspired palette
- basic player/missile DMA or direct-GRAF rendering with positions, sizes,
  two-bit missile shifters, VDELAY, fifth-player and multicolor priority mixing,
  sticky collision latches, HITCLR, and latched joystick triggers
- PIA joystick and PORTB registers
- POKEY keyboard and SKCTL-controlled polynomial RANDOM register; AUDCTL-aware
  15/64/1790 kHz and joined per-channel audio-divider and timer countdowns and
  IRQ edges; CPU-synchronized register playback and sample buffering;
  CPU-clocked pure-tone and reference-tap polynomial distortion/noise;
  cycle-averaged output sampling; latched high-pass; volume-only audio; linear
  four-bit channel-volume mixing with output headroom; and DC filtering
- read-only ATR parsing, sector access, and an XL OS SIOV acceleration for
  disk status and reads
- segmented XEX loading with ordered INITAD execution and RUNAD handoff after
  OS startup
- SDL2 video, audio, keyboard, joystick-key, and console-key frontend
- optional WASMCART packaging of a XEX with framebuffer, audio, joystick,
  console-button, and keyboard adapters

Important current limits: ANTIC/GTIA and DMA steals are scanline approximations
rather than cycle-exact; precise scrolling fetch windows, serial SIO command
transport at the POKEY bit level, Atari cartridge images, save states, and
unstable undocumented CPU instructions are not implemented. WASMCART is a host
package for the existing XEX loader, not emulation of an Atari cartridge.
POKEY serial timing, analog-accurate DAC mixing, and fractional-cycle
resampling remain approximations. The SDL frontend queues one audio block per
emulated frame and starts playback after a small initial buffer, keeping POKEY
register writes synchronized without generating future audio. The SIOV
acceleration lets the stock XL OS boot compatible ATR images without bit-level
serial emulation.
Display-list state, playfield data, colors, character-base, player/missile
data, and collisions now advance one scanline at a time. Register changes
within a scanline are still approximated at its ending boundary.

### Reference audit priorities

A focused comparison with Atari800, A8E, and the local hardware reference
identified compatibility improvements while retaining the flat, small
implementation. The ANTIC pass now honors display-list DMA enable, advances
the display list live rather than predicting DLI lines at frame start, wraps
playfield DMA within 4 KiB, promotes and clips horizontal-scroll fetches,
handles vertical-scroll entry and exit rows, and applies GTIA modes 9, 10, and
11 to ANTIC character modes 2/3 as well as mode F.

The remaining priorities, in recommended order, are:

1. Turn the representative XEX set used during development into a repeatable
   regression corpus. Extend validation beyond the current blank-frame check
   with selected-frame hashes or reference captures, plus audio sample-count,
   peak, and transition summaries. Cover NTSC and PAL where applicable before
   removing or substantially rewriting implemented behavior.
2. Refine CPU/ANTIC timing beyond the current scanline DMA budget and
   instruction-overrun carry;
   page-crossing penalties, stable undocumented opcode families,
   playfield/character/PMG steals, and master-clock advancement during CPU
   pauses are implemented. Memory-refresh steals are omitted until their fixed
   within-scanline placement can be represented without distorting DLI timing.
3. Refine POKEY serial timing and analog mixing beyond the current per-channel
   timer IRQs, CPU-synchronized writes, reference polynomial phases,
   joined-channel handling, high-pass latches, and linear volume response.
4. Model same-scanline player/missile repositioning and shift-register
   retrigger behavior only when a compatibility test demonstrates a need; the
   current sample set does not justify a raster-event subsystem yet.

Lower-priority subsystems such as cartridges, save states, and bit-level SIO
should remain separate additions rather than enlarging the core preemptively.

### Potential simplifications

The following implemented features are candidates for simplification if a
larger automated program corpus and focused hardware tests show no meaningful
compatibility loss. They should not be removed based only on the current small
sample set:

- The synchronized POKEY sample buffer is sized for 8192 samples even though
  the frontend normally drains it every frame. A 2048- or 4096-sample buffer
  would reduce each machine instance by 8-12 KiB if underrun and XEX-handoff
  tests confirm that the additional capacity is never needed.
- POKEY currently drains its contiguous sample buffer with `memmove`. A small
  ring buffer would avoid that copy, but adds indices and wrap handling; keep
  the linear buffer unless profiling shows the frame-sized copy matters.
- `audio_phase[]` now stores integral CPU-cycle countdowns and could become an
  integer array. Likewise, the output-sample accumulator could use fixed-point
  integer arithmetic for deterministic NTSC/PAL sample counts without doubles.
- Add focused producer/consumer tests for the number of samples generated by
  one NTSC and PAL frame before simplifying the POKEY buffer or sample clock.
- Per-channel POKEY timer countdowns could use calculated absolute deadlines if
  timer IRQ tests confirm identical STIMER, AUDCTL, and AUDF behavior.
- Cross-scanline CPU overrun debt could be folded into the scanline DMA budget
  if timing-sensitive display-list and interrupt tests do not distinguish it.
- GTIA modes 9, 10, and 11 on ANTIC character modes 2/3 could be excluded behind
  a build option if a broad software corpus never uses the combination.
- Stable undocumented 6502 opcodes are already optional through
  `A800_UNDOCUMENTED_OPCODES`; a strictly minimal build can disable them, and
  they could eventually be removed if opcode tracing finds no users.

Live display-list processing, display-list DMA gating, 4 KiB ANTIC wrapping,
basic GTIA priority/collisions, and correct BASIC/XEX startup are not current
simplification candidates because they fixed demonstrated behavior. Any
simplification should be measured with `a800-validate`, targeted unit tests,
screen comparisons, and audio traces before and after the change.

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

Stable undocumented NMOS 6502 opcodes are enabled by default. They can be
excluded for a strictly documented instruction set with
`-DA800_UNDOCUMENTED_OPCODES=OFF`.

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
| `wasmcart_main.c` | optional WASMCART framebuffer, audio, and input adapter |

The hardware-named files are intentionally retained even when small. Merging
`Gtia.c` into `Antic.c`, or folding `Pia.c`/`Pokey.c` into `Atari.c`, would mix
register-device behavior with the scanline scheduler and memory bus. The
current boundaries match the requested A8E-style layout and leave room for the
remaining behavior without requiring additional source files.

## WASMCART

An optional Emscripten adapter packages a XEX, the XL OS ROM, and the BASIC ROM
as a WASMCART `.wasc` file. It reuses the normal in-memory XEX loader and keeps
WASMCART-specific code out of the native core build:

```sh
python3 xex2wasc.py game.xex game.wasc "Game name"
```

See [WASMCART.md](WASMCART.md) for prerequisites, assets, and exported devices.

## Validation utility

The build also produces `a800-validate`, a headless replacement for the small
one-off boot and framebuffer probes used during development. It reports CPU,
banking, ANTIC, GTIA, POKEY audio/control registers, and framebuffer state. It
can run for an emulated duration and save the final frame as a dependency-free
binary portable pixmap (PPM):

```sh
./build/a800-validate --basic --frames 180
./build/a800-validate --frames 300 program.xex
./build/a800-validate --seconds 5 --output frame.ppm program.xex
./build/a800-validate --seconds 8 --start-frame 200 game.xex
./build/a800-validate --frames 300 --output frame.ppm disk.atr
./build/a800-validate --pal --frames 180
```

Use `--os` and `--basic-rom` to select ROM files. The utility returns failure
for invalid arguments or ROM/media/output errors. A completed capture returns
1 when every framebuffer pixel has the same color and 0 when the frame contains
more than one color, making the command directly usable in automated program
smoke tests. When the frame is blank (a single color), a file requested with
`--output` is not written. `--xex-frame N` overrides the default frame-85 XEX
handoff; `--start-frame N` presses START for one frame; and `--trace-audio`
reports POKEY register changes. The blank-frame result is a smoke test, not a
complete visual regression check; the regression-corpus priority above is
intended to cover nonblank-but-incorrect output.

## Provenance

The code in this directory was written for this simplified implementation; it
does not copy A8E source. Atari800 (GPL-2.0-or-later) was used as an
architectural and behavioral reference, especially its CPU, memory, ANTIC,
GTIA, POKEY, and PIA module boundaries. See <https://atari800.github.io/>.
