#!/usr/bin/env python3
"""Convert an Atari OS ROM into a sparse a800 ROM-XEX image."""

import argparse
import os
import struct
import sys


MAGIC = b"A8OSXEX\0"
MACHINES = {"800": 1, "xl": 2, "5200": 3}


def machine_from_name(path):
    name = os.path.basename(path).lower()
    if "5200" in name:
        return "5200"
    if "osa" in name or "os-a" in name or "osb" in name or "os-b" in name:
        return "800"
    if "xl" in name or "xe" in name:
        return "xl"
    raise ValueError("cannot infer ROM type from filename (expected OSA, OSB, XL/XE, or 5200)")


def block(address, data):
    if not data:
        return b""
    end = address + len(data) - 1
    return struct.pack("<HH", address, end) + data


def convert(path, no_self_test=False, no_floating_point=False):
    machine = machine_from_name(path)
    with open(path, "rb") as source:
        rom = source.read()
    expected = {"800": 0x2800, "xl": 0x4000, "5200": 0x0800}[machine]
    if len(rom) != expected:
        raise ValueError(
            f"{machine} ROM must be {expected} bytes, got {len(rom)}"
        )
    if machine != "xl" and no_self_test:
        raise ValueError("self-test omission applies only to XL/XE ROMs")
    if machine == "5200" and no_floating_point:
        raise ValueError("floating-point omission does not apply to 5200")

    flags = (1 if no_self_test else 0) | (2 if no_floating_point else 0)
    metadata = MAGIC + bytes((1, MACHINES[machine], flags, 0, 0, 0, 0, 0))
    result = bytearray(b"\xff\xff" + block(0, metadata))
    if machine == "xl":
        result += block(0xC000, rom[:0x1000])
        if not no_self_test:
            result += block(0x5000, rom[0x1000:0x1800])
        if not no_floating_point:
            result += block(0xD800, rom[0x1800:0x2000])
        result += block(0xE000, rom[0x2000:])
    elif machine == "800":
        if not no_floating_point:
            result += block(0xD800, rom[:0x0800])
        result += block(0xE000, rom[0x0800:])
    else:
        result += block(0xF800, rom)
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(
        description="Convert an OSA, OSB, XL/XE, or 5200 ROM to sparse ROM-XEX"
    )
    parser.add_argument("--no-self-test", action="store_true")
    parser.add_argument("--no-floating-point", action="store_true")
    parser.add_argument("input_rom")
    parser.add_argument("output_xex")
    args = parser.parse_args()
    try:
        image = convert(
            args.input_rom,
            no_self_test=args.no_self_test,
            no_floating_point=args.no_floating_point,
        )
        with open(args.output_xex, "wb") as output:
            output.write(image)
    except (OSError, ValueError) as error:
        print(f"rom2xex.py: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
