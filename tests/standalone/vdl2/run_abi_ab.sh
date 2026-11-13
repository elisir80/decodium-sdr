#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
build="$root/build/vdl2-isolated"
reference=${ONAIR_RUST_LIB:-$root/build/vdl2-audit/onair-build/rust/release/libonair_rs.a}
export CARGO_TARGET_DIR="$build/target" DSDR_TEST_SILENT_AUDIO=1
mkdir -p "$build"
(cd "$root/src/dsp/vdl2/rust" && "${CARGO:-cargo}" build --offline --release -p dsdr_vdl2)
${CXX:-c++} -std=c++17 -O2 -I"$root/src" "$here/abi_ab.cpp" "$reference" -o "$build/reference"
${CXX:-c++} -std=c++17 -O2 -I"$root/src" "$here/abi_ab.cpp" "$build/target/release/libdsdr_vdl2.a" -o "$build/isolated"
"$build/reference" > "$build/reference.txt"
"$build/isolated" > "$build/isolated.txt"
cmp "$build/reference.txt" "$build/isolated.txt"
cat "$build/isolated.txt"
