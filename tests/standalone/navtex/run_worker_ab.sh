#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
build="$root/build/navtex-worker-ab"
mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
case "$(uname -s)" in Darwin) stripflag=-Wl,-dead_strip ;; *) stripflag=-Wl,--gc-sections ;; esac
${CXX:-c++} -std=c++20 -O2 -pthread -ffunction-sections -fdata-sections -I"$root/src" -I"$onair/core/include" \
  "$here/worker_ab.cpp" "$root/src/core/NavtexReceiver.cpp" "$root/src/core/AsyncIqDecoder.cpp" \
  "$root/src/dsp/navtex/NavtexAudioDecoder.cpp" "$root/src/dsp/navtex/NavtexProtocol.cpp" \
  "$onair/core/src/marine/marine_navtex.cpp" "$onair/core/src/marine/marine_dsp.cpp" \
  "$stripflag" -o "$build/worker_ab"
"$build/worker_ab"
