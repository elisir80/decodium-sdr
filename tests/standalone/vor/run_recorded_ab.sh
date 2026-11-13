#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
input=${1:?Pass directory containing the three documented WAV recordings}
build="$root/build/vor-recorded-ab"
mkdir -p "$build"
export DSDR_TEST_SILENT_AUDIO=1
${CXX:-c++} -std=c++17 -O2 -I"$root/src" "$here/recorded_probe.cpp" \
  "$root/src/dsp/VorReceiver.cpp" "$root/src/dsp/VorIdentDecoder.cpp" -o "$build/probe"
run_case() {
    name=$1 expected_hash=$2 expected_radial=$3
    wav="$input/$name"
    actual=$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1],"rb").read()).hexdigest())' "$wav")
    [ "$actual" = "$expected_hash" ] || { printf '%s\n' "unexpected hash for $name" >&2; exit 1; }
    ffmpeg -v error -i "$wav" -ac 1 -ar 48000 -f f32le -y "$build/native.f32"
    "$build/probe" "$build/native.f32" "$expected_radial" 2
}
run_case 177deg_long_1.wav 5dd89788e65eb16c0bd679d4afb56085ca6389c26908d37bea0deaaff1995be5 155.5
run_case 234deg_long_1.wav a9741d6e8f31f66891ee9b6db846ae721f95d5f38dae4645bbd7fe55b758345a 211.5
run_case 293deg_long_1.wav b07b3cc0ac392fe0d1697756e716c92a9cfe950cc59ac2002046879017a668ad 270.2
