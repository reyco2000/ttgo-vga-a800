# ROM-XEX images

`rom2xex.py` converts Atari OSA, OSB, XL/XE, and 5200 OS dumps into a sparse
XEX-shaped ROM image. The input type is inferred from a case-insensitive
filename containing `OSA`, `OSB`, `XL`/`XE`, or `5200`:

```sh
python3 rom2xex.py ATARIXL.ROM OS.XEX
python3 rom2xex.py --no-self-test --no-floating-point ATARIXL.ROM OS-small.XEX
python3 rom2xex.py ATARIOSA.ROM OSA.XEX
python3 rom2xex.py 5200.ROM 5200-OS.XEX
```

The output uses ordinary XEX segment framing. Its first `$0000-$000f` segment
contains the `A8OSXEX` signature, format version, machine profile, and omission
flags. Remaining blocks use their runtime addresses. They are loaded into ROM
storage rather than RAM:

| Profile | Input size | ROM-XEX blocks |
| --- | ---: | --- |
| OSA/OSB (`400/800`) | 10 KiB | `$d800-$ffff` |
| XL/XE | 16 KiB | `$c000-$cfff`, banked self-test at `$5000-$57ff`, `$d800-$ffff` |
| 5200 | 2 KiB | `$f800-$ffff` |

`--no-self-test` removes the independently banked XL self-test block.
`--no-floating-point` removes the standard `$d800-$dfff` math-pack block from
OSA, OSB, or XL images. These options make an image suitable only for software
that does not call the omitted entry points.

Printer, cassette, modem, and disk removal is intentionally not offered yet.
Those handlers share ROM pages and common routines in the original Atari ROMs;
reliably removing them requires per-ROM symbol maps and patched vectors, not
merely omitting a sparse XEX block.

Use a ROM-XEX anywhere an OS ROM is accepted:

```sh
./build/a800 --os OS.XEX program.xex
python3 xex2wasc.py --os OS.XEX program.xex program.wasc
```

WASMCART packages the selected image under the fixed asset name `OS.XEX`.

The machine profile changes the OS and hardware memory maps. Current 5200
support covers its OS, GTIA, ANTIC, and POKEY address maps; cartridge mapping,
analog controllers, and the keypad remain separate future work.
