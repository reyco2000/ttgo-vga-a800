# WASMCART

`a800` can package an Atari XEX as a `.wasc` cartridge using Emscripten and
`wasmcart-pack`. The cartridge embeds `ATARIXL.ROM` and the XEX as assets, boots
the XL OS, then hands the in-memory XEX to the normal segmented loader at the
same frame used by the desktop frontend. BASIC is omitted by default.

```sh
python3 xex2wasc.py game.xex game.wasc "Game name"
python3 xex2wasc.py --pal game.xex game-pal.wasc "Game name"
python3 xex2wasc.py -b game.xex game-with-basic.wasc "Game name"
```

Place the 16 KiB `ATARIXL.ROM` in this directory. To include and load BASIC,
also provide the 8 KiB `ATARIBAS.ROM` and pass `-b`; without `-b`, that file is
neither read nor packaged. The script requires `emcc`, `npx`, and the
`wasmcart-pack` package. The adapter
exports a 336x240 cropped framebuffer, float32 stereo audio, one joystick,
console buttons, and USB-HID keyboard input. NTSC is the default; `--pal`
bakes PAL timing and its palette into the cartridge, while `--ntsc` selects
NTSC explicitly. The unused palette is omitted from each optimized WASM build.

The shared `wasmcart.h` and `wc_cart.h` files define wasmcart ABI version 3 and
the standard cartridge buffers. `wasmcart_main.c` contains the a800-specific
adapter. The desktop build does not compile these files.
