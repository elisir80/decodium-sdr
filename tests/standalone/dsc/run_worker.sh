#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
build="$root/build/dsc-worker-standalone"
mkdir -p "$build/cache" "$build/tmp"
export XDG_CACHE_HOME="$build/cache"
export TMPDIR="$build/tmp"
export DSDR_TEST_SILENT_AUDIO=1
export QT_QPA_PLATFORM=offscreen
cmake -S "$here" -B "$build" -DONAIR_ROOT="${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}" -DCMAKE_PREFIX_PATH="${DSC_QT_PREFIX:-/opt/homebrew/opt/qt}"
cmake --build "$build" -j 4
"$build/dsc_worker_ui"
