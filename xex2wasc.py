#!/usr/bin/env python3
"""Build the compact a800 core and package one XEX as a wasmcart cartridge."""
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 xex2wasc.py <input.xex> <output.wasc> [game_name]")
        return 1
    source_dir = os.path.dirname(os.path.abspath(__file__))
    input_xex = os.path.abspath(sys.argv[1])
    output_wasc = os.path.abspath(sys.argv[2])
    game_name = sys.argv[3] if len(sys.argv) > 3 else os.path.splitext(os.path.basename(input_xex))[0]
    if not os.path.isfile(input_xex):
        print(f"Error: input XEX not found: {input_xex}")
        return 1
    assets = {name: os.path.join(source_dir, name) for name in ("ATARIBAS.ROM", "ATARIXL.ROM")}
    missing = [path for path in assets.values() if not os.path.isfile(path)]
    if missing:
        print("Error: ATARIBAS.ROM and ATARIXL.ROM must be in the a800 directory")
        return 1
    wasm = os.path.join(source_dir, "cart.wasm")
    command = [
        "emcc", "-O3", "-DWASMCART", "-I.",
        "-s", "WASM=1", "-s", "SIDE_MODULE=0", "-s", "ALLOW_MEMORY_GROWTH=1",
        "-s", "INITIAL_MEMORY=33554432",
        "-s", 'EXPORTED_FUNCTIONS=["_wc_get_info","_wc_init","_wc_render"]',
        "-s", "ERROR_ON_UNDEFINED_SYMBOLS=0", "-s", "WARN_ON_UNDEFINED_SYMBOLS=0",
        "-o", wasm, "6502.c", "Atari.c", "Antic.c", "AtariIo.c", "Gtia.c",
        "Pia.c", "Pokey.c", "wasmcart_main.c", "-lm",
    ]
    temporary = tempfile.mkdtemp(prefix="a800-wasc-")
    try:
        print("Compiling a800 wasm cartridge...")
        subprocess.run(command, cwd=source_dir, check=True)
        shutil.copy(assets["ATARIBAS.ROM"], os.path.join(temporary, "ATARIBAS.ROM"))
        shutil.copy(assets["ATARIXL.ROM"], os.path.join(temporary, "ATARIXL.ROM"))
        shutil.copy(input_xex, os.path.join(temporary, "game.xex"))
        subprocess.run([
            "npx", "wasmcart-pack", "--wasm", wasm, "--assets", temporary,
            "--width", "336", "--height", "240", "--players", "1",
            "--name", game_name, "--output", output_wasc,
        ], check=True)
        print(f"Created {output_wasc}")
        return 0
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}")
        return 1
    finally:
        shutil.rmtree(temporary)
        try:
            os.remove(wasm)
        except OSError:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
