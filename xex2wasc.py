#!/usr/bin/env python3
"""Build the compact a800 core and package one XEX as a wasmcart cartridge."""
import argparse
import os
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(
        description="Package an Atari XEX as an a800 WASMCART cartridge"
    )
    parser.add_argument(
        "-b",
        "--basic",
        action="store_true",
        help="include and load ATARIBAS.ROM",
    )
    parser.add_argument("input_xex")
    parser.add_argument("output_wasc")
    parser.add_argument("game_name", nargs="?")
    args = parser.parse_args()
    source_dir = os.path.dirname(os.path.abspath(__file__))
    input_xex = os.path.abspath(args.input_xex)
    output_wasc = os.path.abspath(args.output_wasc)
    game_name = args.game_name or os.path.splitext(os.path.basename(input_xex))[0]
    if not os.path.isfile(input_xex):
        print(f"Error: input XEX not found: {input_xex}")
        return 1
    names = ["ATARIXL.ROM"]
    if args.basic:
        names.append("ATARIBAS.ROM")
    assets = {name: os.path.join(source_dir, name) for name in names}
    missing = [path for path in assets.values() if not os.path.isfile(path)]
    if missing:
        print("Error: required ROM file not found: " + ", ".join(missing))
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
    if args.basic:
        command.insert(3, "-DWASMCART_BASIC")
    temporary = tempfile.mkdtemp(prefix="a800-wasc-")
    try:
        print("Compiling a800 wasm cartridge...")
        subprocess.run(command, cwd=source_dir, check=True)
        shutil.copy(assets["ATARIXL.ROM"], os.path.join(temporary, "ATARIXL.ROM"))
        if args.basic:
            shutil.copy(assets["ATARIBAS.ROM"], os.path.join(temporary, "ATARIBAS.ROM"))
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
