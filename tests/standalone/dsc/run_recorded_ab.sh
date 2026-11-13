#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
recording=${1:?Pass the external test.wav recording}
build="$root/build/dsc-recorded-ab"
mkdir -p "$build"
expected=3e170987f154a0769fa22ca94c1fe45e6083de9f78c90c24420055edae5fe2e0
actual=$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$recording")
[ "$actual" = "$expected" ] || { printf '%s\n' 'Unexpected recording hash' >&2; exit 1; }
export DSDR_TEST_SILENT_AUDIO=1
ffmpeg -v error -i "$recording" -af 'pan=mono|c0=c0,afreqshift=shift=1200' -ar 24000 -f f32le -y "$build/translated.f32"
case "$(uname -s)" in Darwin) stripflag=-Wl,-dead_strip ;; *) stripflag=-Wl,--gc-sections ;; esac
${CXX:-c++} -std=c++17 -O2 -ffunction-sections -fdata-sections -DDECT2_PORTABLE -I"$root/src" -I"$onair/core/include" \
  "$here/test_recorded_ab.cpp" "$root/src/dsp/dsc/DscRx.cpp" "$root/src/dsp/dsc/FskAudioRx.cpp" \
  "$onair/core/src/marine/marine_dsc.cpp" "$onair/core/src/marine/marine_mmsi.cpp" "$onair/core/src/marine/marine_dsp.cpp" \
  "$stripflag" -o "$build/test_recorded_ab"
"$build/test_recorded_ab" "$build/translated.f32"
