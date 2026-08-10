# WASMCART

`a800` can package an Atari XEX as a `.wasc` cartridge using Emscripten and
`wasmcart-pack`. The cartridge embeds `ATARIXL.ROM`, `ATARIBAS.ROM`, and the XEX
as assets, boots the XL OS, then hands the in-memory XEX to the normal segmented
loader at the same frame used by the desktop frontend.

```sh
python3 xex2wasc.py game.xex game.wasc "Game name"
```

Place the 16 KiB `ATARIXL.ROM` and 8 KiB `ATARIBAS.ROM` files in this directory.
The script requires `emcc`, `npx`, and the `wasmcart-pack` package. The adapter
exports a 336x240 cropped framebuffer, float32 stereo audio, one joystick,
console buttons, and USB-HID keyboard input. NTSC is the default.

The shared `wasmcart.h` and `wc_cart.h` files define wasmcart ABI version 3 and
the standard cartridge buffers. `wasmcart_main.c` contains the a800-specific
adapter. The desktop build does not compile these files.
