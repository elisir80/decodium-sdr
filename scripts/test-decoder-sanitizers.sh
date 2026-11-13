#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Headless decoder checks: no audio device or radio is opened.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
out=${DSDR_SANITIZER_BUILD_DIR:-"$root/build/decoder-sanitizers"}
cxx=${CXX:-clang++}
mkdir -p "$out"
export DSDR_TEST_SILENT_AUDIO=1
export ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}
export UBSAN_OPTIONS=${UBSAN_OPTIONS:-halt_on_error=1}
"$cxx" -O1 -g -std=c++17 -fsanitize=address,undefined -I"$root/src" \
    "$root/tests/standalone/tst_aprs_afsk.cpp" "$root/src/dsp/AprsAfskDecoder.cpp" \
    -o "$out/aprs-afsk"
"$out/aprs-afsk"
"$cxx" -O1 -g -std=c++17 -fsanitize=address,undefined -I"$root/src" \
    "$root/tests/standalone/pocsag/test.cpp" "$root/src/dsp/PocsagDecoder.cpp" \
    -o "$out/pocsag"
"$out/pocsag"
"$cxx" -O1 -g -std=c++17 -fsanitize=address,undefined -I"$root/src" \
    "$root/tests/standalone/pocsag/test_audio.cpp" "$root/src/dsp/PocsagDecoder.cpp" \
    "$root/src/dsp/PocsagAudioDecoder.cpp" -o "$out/pocsag-audio"
"$out/pocsag-audio"
"$cxx" -O1 -g -std=c++17 -fsanitize=address,undefined -I"$root/src" \
    "$root/tests/standalone/pocsag/test_iq.cpp" "$root/src/dsp/PocsagDecoder.cpp" \
    "$root/src/dsp/PocsagAudioDecoder.cpp" "$root/src/dsp/PocsagIqReceiver.cpp" \
    -o "$out/pocsag-iq"
"$out/pocsag-iq"
"$cxx" -O1 -g -std=c++17 -fsanitize=address,undefined -I"$root/src" \
    -I"$root/tests/standalone" "$root/tests/standalone/tst_ais_decoder.cpp" \
    "$root/src/dsp/AisDecoder.cpp" -o "$out/ais"
"$out/ais"
"$cxx" -O1 -g -std=c++17 -fsanitize=address,undefined -pthread -I"$root/src" \
    "$root/tests/standalone/acars/test_acars.cpp" "$root/src/core/AsyncIqDecoder.cpp" \
    "$root/src/core/AcarsReceiver.cpp" "$root/src/dsp/acars/acars_chan.cpp" \
    -o "$out/acars"
"$out/acars"
