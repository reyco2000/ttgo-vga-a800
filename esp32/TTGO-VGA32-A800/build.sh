#!/usr/bin/env bash
# Build (and optionally flash) the TTGO VGA32 frontend.
#
#   esp32/TTGO-VGA32-A800/build.sh            compile only
#   esp32/TTGO-VGA32-A800/build.sh /dev/ttyACM0   compile and upload
#
# Requires arduino-cli with esp32:esp32@2.0.17 (FabGL 1.0.9 does not build on
# core 3.x) and the FabGL library. The shared core is copied into src/core
# because the Arduino builder only compiles sources inside the sketch folder.
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SKETCH_DIR/../.." && pwd)"
FQBN="esp32:esp32:esp32wrover:PartitionScheme=huge_app"
# Must be identical for C and C++ so both see the same Atari layout.
CORE_FLAGS="-include esp_attr.h -DA800_HOT=IRAM_ATTR -DA800_NO_PSRAM_MEMW=1 -DA800_LINE_BUFFER=1 -DA800_AUDIO_BUFFER=1024 -DNO_SAVE_STATE=1 -DA800_UNDOCUMENTED_OPCODES=1"

mkdir -p "$SKETCH_DIR/src/core"
rm -f "$SKETCH_DIR/src/core/"*.[ch]
for f in 6502 Atari Antic AtariIo Gtia Pia Pokey; do
  cp "$ROOT/$f.c" "$ROOT/$f.h" "$SKETCH_DIR/src/core/"
done
cp "$ROOT/Palette.h" "$SKETCH_DIR/src/core/"

arduino-cli compile --fqbn "$FQBN" \
  --build-path "$SKETCH_DIR/build" \
  --build-property "compiler.optimization_flags=-O2" \
  --build-property "compiler.c.extra_flags=$CORE_FLAGS" \
  --build-property "compiler.cpp.extra_flags=$CORE_FLAGS" \
  --output-dir "$SKETCH_DIR/build/out" \
  "$SKETCH_DIR"

if [ $# -ge 1 ]; then
  arduino-cli upload --fqbn "$FQBN" -p "$1" --input-dir "$SKETCH_DIR/build/out" "$SKETCH_DIR"
fi
