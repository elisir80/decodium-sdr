#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
build="$root/build/onair-ab"
mkdir -p "$build"
${CXX:-c++} -std=c++17 -O2 -DDECT2_PORTABLE -I"$root/src" -I"$onair/core/include" \
  "$here/ab.cpp" "$root/src/dsp/AdsbDecoder.cpp" "$root/src/core/AsyncIqDecoder.cpp" "$onair/core/src/adsb/adsb_demod.cpp" \
  "$onair/core/src/dsp/dsp_compat.cpp" -o "$build/ab"
"$build/ab"
${CXX:-c++} -std=c++17 -O2 -DDECT2_PORTABLE -I"$root/src" -I"$onair/core/include" \
  "$here/real_ab.cpp" "$root/src/dsp/AdsbDecoder.cpp" "$onair/core/src/adsb/adsb_demod.cpp" \
  "$onair/core/src/dsp/dsp_compat.cpp" -o "$build/real_ab"
"$build/real_ab" "$onair/tests/data/adsb/modes1_2msps_cu8.bin"
