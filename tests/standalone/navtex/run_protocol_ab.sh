#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
build="$root/build/navtex-protocol-ab"
mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
${CXX:-c++} -std=c++20 -O2 -I"$root/src" -I"$onair/core/include" \
  "$here/protocol_ab.cpp" "$root/src/dsp/navtex/NavtexProtocol.cpp" \
  "$onair/core/src/marine/marine_navtex.cpp" -o "$build/protocol_ab"
"$build/protocol_ab"
