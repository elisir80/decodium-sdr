#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Reference-only tests: no receiver, output device, or application is started.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../../.." && pwd)
onair=${ONAIR_ROOT:-/Volumes/Projects/OnAir-reference-3.9.0}
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$root/build/vdl2-audit/target}"
export DSDR_TEST_SILENT_AUDIO=1
mkdir -p "$CARGO_TARGET_DIR"
cd "$onair/rust"
"${CARGO:-cargo}" test --offline -p onair_vdl2
