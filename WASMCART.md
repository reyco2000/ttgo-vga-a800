# WASMCART

`a800` can package an Atari XEX as a `.wasc` cartridge using Emscripten and
`wasmcart-pack`. The cartridge embeds `ATARIXL.ROM` and the XEX as assets, boots
the XL OS, then hands the in-memory XEX to the normal segmented loader at the
same frame used by the desktop frontend. BASIC is omitted by default.

```sh
python3 xex2wasc.py game.xex game.wasc "Game name"
python3 xex2wasc.py --pal game.xex game-pal.wasc "Game name"
python3 xex2wasc.py -b game.xex game-with-basic.wasc "Game name"
python3 xex2wasc.py --no-open-editor game.xex game-small.wasc "Game name"
```

Place the 16 KiB `ATARIXL.ROM` in this directory. To include and load BASIC,
also provide the 8 KiB `ATARIBAS.ROM` and pass `-b`; without `-b`, that file is
neither read nor packaged. The script requires `emcc`, `npx`, and the
`wasmcart-pack` package. The adapter
exports a 336x240 cropped framebuffer, float32 stereo audio, one joystick,
console buttons, and USB-HID keyboard input. NTSC is the default; `--pal`
bakes PAL timing and its palette into the cartridge, while `--ntsc` selects
NTSC explicitly. The packager defines `NO_PAL` or `NO_NTSC`, respectively, so
the unused standard and palette are omitted. It defines `NO_BASIC` unless `-b`
is passed.

The normal build opens the OS `E:` device before starting the XEX, matching the
editor initialization performed during a regular XL boot. `--no-open-editor`
omits that compatibility step for a smaller cartridge. Diagnostic state code is
omitted by default; `--save-state` includes it for custom WASMCART integrations,
although the standard adapter does not expose snapshot controls.

The shared `wasmcart.h` and `wc_cart.h` files define wasmcart ABI version 3 and
the standard cartridge buffers. `wasmcart_main.c` contains the a800-specific
adapter. The desktop build does not compile these files.
