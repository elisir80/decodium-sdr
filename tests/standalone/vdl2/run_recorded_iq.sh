#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
input=${1:?Pass the external VDL2 IQ sample}
expected=c4e8de5e1d805ace3905107bf27ce1e7130f5f95db321c4ff86a16f08669f7fa
actual=$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$input")
[ "$actual" = "$expected" ] || { printf '%s\n' 'Unexpected IQ sample hash' >&2; exit 1; }
rustlib=${DSDR_VDL2_LIB:-"$root/build/vdl2-on/vdl2-rust/release/libdsdr_vdl2.a"}
build="$root/build/vdl2-recorded-ab"; mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
${CXX:-c++} -std=c++17 -O2 -I"$root/src" "$here/recorded_iq_probe.cpp" \
  "$root/src/dsp/vdl2/Vdl2Acquisition.cpp" "$root/src/dsp/vdl2/Vdl2Burst.cpp" \
  "$root/src/dsp/vdl2/Vdl2Symbols.cpp" "$root/src/dsp/vdl2/Vdl2Offline.cpp" \
  "$root/src/dsp/vdl2/Vdl2WindowStream.cpp" "$root/src/dsp/vdl2/Vdl2Frame.cpp" \
  "$rustlib" -o "$build/probe"
"$build/probe" "$input"
ffmpeg -v error -f s16le -ar 50000 -ac 2 -i "$input" -ar 192000 -f f32le -y "$build/upsampled.cf32"
${CXX:-c++} -std=c++17 -O2 -I"$root/src" "$here/recorded_frontend_probe.cpp" \
  "$root/src/dsp/vdl2/Vdl2Frontend.cpp" "$root/src/dsp/vdl2/Vdl2Acquisition.cpp" \
  "$root/src/dsp/vdl2/Vdl2Burst.cpp" "$root/src/dsp/vdl2/Vdl2Symbols.cpp" \
  "$root/src/dsp/vdl2/Vdl2Offline.cpp" "$root/src/dsp/vdl2/Vdl2WindowStream.cpp" \
  "$root/src/dsp/vdl2/Vdl2Frame.cpp" "$rustlib" -o "$build/frontend_probe"
"$build/frontend_probe" "$build/upsampled.cf32"
