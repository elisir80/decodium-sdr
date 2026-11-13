#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
build="$root/build/dsc-standalone"
mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
${CXX:-c++} -std=c++17 -O2 -ffunction-sections -fdata-sections -DDECT2_PORTABLE -I"$onair/core/include" \
  "$here/test_audio_ab.cpp" "$here/DscRx.cpp" "$here/FskAudioRx.cpp" \
  "$onair/core/src/marine/marine_dsc.cpp" "$onair/core/src/marine/marine_mmsi.cpp" \
  "$onair/core/src/marine/marine_dsp.cpp" -Wl,-dead_strip -o "$build/test_audio_ab"
"$build/test_audio_ab"
