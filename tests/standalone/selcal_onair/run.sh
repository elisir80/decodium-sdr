#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
build="$root/build/selcal-onair-ab"
mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
case "$(uname -s)" in
  Darwin) stripflag=-Wl,-dead_strip ;;
  *) stripflag=-Wl,--gc-sections ;;
esac
${CXX:-c++} -std=c++17 -O2 -ffunction-sections -fdata-sections -DDECT2_PORTABLE \
  -I"$root/src" -I"$onair/core/include" \
  "$here/ab.cpp" "$here/silent_audio.cpp" "$root/src/dsp/SelcalAudioDecoder.cpp" \
  "$onair/core/src/selcal/selcal_rx.cpp" "$onair/core/src/tones/tones_rx.cpp" \
  "$onair/core/src/tones/tones_proto.cpp" "$onair/core/src/dsp/exact_resampler.cpp" \
  "$onair/core/src/packet/packet_demod.cpp" "$onair/core/src/marine/marine_dsp.cpp" \
  "$onair/core/src/dsp/dsp_compat.cpp" \
  "$stripflag" -o "$build/ab"
"$build/ab" "$@"
