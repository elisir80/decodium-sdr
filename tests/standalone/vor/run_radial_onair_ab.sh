#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
build="$root/build/vor-radial-ab"
mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
case "$(uname -s)" in Darwin) stripflag=-Wl,-dead_strip ;; *) stripflag=-Wl,--gc-sections ;; esac
${CXX:-c++} -std=c++17 -O2 -ffunction-sections -fdata-sections -I"$root/src" -I"$onair/core/include" \
    "$here/test_radial_onair.cpp" "$root/src/dsp/VorIdentDecoder.cpp" "$root/src/dsp/VorReceiver.cpp" "$onair/core/src/vor/vor_gen.cpp" \
    "$stripflag" -o "$build/test_radial_onair"
"$build/test_radial_onair"
