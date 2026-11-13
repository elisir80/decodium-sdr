#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
input=${1:?Pass the external ICAO SELCAL MP3 sample}
expected=4a9d93f70bb04bc78c67ed0e3f17459b8f600fa1cddab0eabc318748d5898835
actual=$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$input")
[ "$actual" = "$expected" ] || { printf '%s\n' 'Unexpected recording hash' >&2; exit 1; }
build="$root/build/selcal-recorded-ab"; mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
ffmpeg -v error -i "$input" -ac 1 -ar 8000 -f f32le -y "$build/recording.f32"
case "$(uname -s)" in Darwin) stripflag=-Wl,-dead_strip ;; *) stripflag=-Wl,--gc-sections ;; esac
${CXX:-c++} -std=c++17 -O2 -ffunction-sections -fdata-sections -DDECT2_PORTABLE -I"$root/src" -I"$onair/core/include" \
  "$here/recorded_ab.cpp" "$here/silent_audio.cpp" "$root/src/dsp/SelcalAudioDecoder.cpp" \
  "$onair/core/src/selcal/selcal_rx.cpp" "$onair/core/src/tones/tones_rx.cpp" "$onair/core/src/tones/tones_proto.cpp" \
  "$onair/core/src/dsp/exact_resampler.cpp" "$onair/core/src/packet/packet_demod.cpp" \
  "$onair/core/src/marine/marine_dsp.cpp" "$onair/core/src/dsp/dsp_compat.cpp" "$stripflag" -o "$build/recorded_ab"
"$build/recorded_ab" "$build/recording.f32"
