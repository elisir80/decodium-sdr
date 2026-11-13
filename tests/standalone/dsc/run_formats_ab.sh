#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
build="$root/build/dsc-product-test"
mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
case "$(uname -s)" in Darwin) stripflag=-Wl,-dead_strip ;; *) stripflag=-Wl,--gc-sections ;; esac
${CXX:-c++} -std=c++17 -O2 -ffunction-sections -fdata-sections -I"$root/src" -I"$onair/core/include" \
  "$here/test_formats_ab.cpp" "$root/src/dsp/dsc/DscRx.cpp" \
  "$onair/core/src/marine/marine_dsc.cpp" "$onair/core/src/marine/marine_mmsi.cpp" \
  "$onair/core/src/marine/marine_dsp.cpp" "$stripflag" -o "$build/test_formats_ab"
"$build/test_formats_ab"
