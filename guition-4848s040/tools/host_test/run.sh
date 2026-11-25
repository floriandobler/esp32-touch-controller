#!/bin/sh
# Baut das Testprogramm am Rechner und lädt eine .tosc-Datei damit.
# Aufruf (im Projektordner): ./tools/host_test/run.sh data/layout.tosc
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
SRC="$DIR/../../src"
OUT="${TMPDIR:-/tmp}/tosc_host_test"
MBEDTLS="$(brew --prefix mbedtls)"  # wie auf dem ESP32 (mbedtls 3.6)
c++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
  "$DIR/host_test.cpp" "$SRC/tosc.cpp" "$SRC/xml_mini.cpp" "$SRC/osc.cpp" "$SRC/dmx.cpp" "$SRC/tuya_proto.cpp" \
  -I"$MBEDTLS/include" "$MBEDTLS/lib/libmbedcrypto.a" -lz -o "$OUT"
"$OUT" "$@"
