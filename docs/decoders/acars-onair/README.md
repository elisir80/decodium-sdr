# ACARS VHF single-channel standalone RX

This is an isolated component, not registered with the application, UI or shared
build. It accepts complex IQ at 1–4 MS/s and a relative channel offset; it mixes,
filters, AM envelope-detects, demodulates 2400-bit/s MSK and frames ACARS
blocks. The caller supplies an IQ stream with a usable channel and enough
passband; no channel scanning, source tuning, squelch or input resampling is
provided. Outputs are parity-bearing bytes from mode through ETX/ETB, excluding
SOH and the two BCS bytes. They have passed odd parity, CRC-16/KERMIT and basic
layout checks. No correction is attempted and rejected blocks are not delivered.
The component does not perform label-specific decoding or multi-block assembly.
A CRC and syntax check do not authenticate a message.

`AcarsReceiver` runs the channel decoder on `AsyncIqDecoder`, with bounded input
and result queues. Its `start`, `stop`, and destruction require producer and
consumer quiescence; `stop` discards pending input and joins, so run it off the
DSP and UI threads. Exactly one producer calls `submitForGeneration`, and one
consumer calls `read`. Serialize control setters with each other. Capture
`generation()` **before acquiring the samples**; use that generation on submit.
Call `discontinuity()` for retunes, source changes or loss not represented by
`setSource`. Failed submissions invalidate the generation and cause the next
accepted block to start a fresh decoder. Results from previous generations are
discarded. A nonfinite IQ sample invalidates the generation and increments
`workerErrors`. Queue drops are exposed by `stats()` and `droppedMessages()`.
The host must provide its own safe shutdown barrier and source-lifetime control.
There is no guarantee against a worker blocked indefinitely in a callback.

## Provenance and attribution

`src/dsp/acars/acars_chan.{h,cpp}` derives from OnAir, by Kristijonas Adomaitis
and OnAir contributors, https://github.com/adswill/OnAir, local reference
`/Volumes/Projects/OnAir-reference-3.9.0`, commit
`15a2907f02f7486fc64b8e439a1645eb7373171f`. OnAir's README and REUSE.toml
identify its own code as GPL-3.0-or-later; its `ADDITIONAL-TERMS.md` requires
retaining the accompanying terms and the following unchanged attribution in
the program's Appropriate Legal Notices:

> Based on OnAir by Kristijonas Adomaitis - https://on-air.dev

The [additional terms](ADDITIONAL-TERMS.md) are retained alongside this notice.
**Any future application integration or distribution must put that exact line in
its About, Credits or Licence screen (or the applicable CLI legal notices)**;
this standalone change does not modify an application screen. The standalone
test prints it. Retain the GPL licence and source notices upon distribution.
This is a technical provenance review, not a legal opinion.

Original SHA-256: `acars_chan.cpp`
`f06d786054012df053c8e3b89ecaf00d7f7f68454b7e0a94c45fa8264f1d44c9`;
`acars_chan.h` `1640c7be8759365c90597ca140851bed7edb803f9e2f5811ba9830d1cb78ae93`.
Changes: replace OnAir's broad IQ and DSP includes with local types, use the
original default FIR taps rather than its user-overridable filter, inline its
Bessel series from `core/include/dect2/common/window.h` (same OnAir licence),
and replace the Rust-backed parity/CRC error-repair interface with the local
strict checker. No libacars or acarsdec source is included. The independent test
generator uses the published signal format and a separate table-based CRC;
protocol references: https://web.stanford.edu/~pauly/ee179/Lab8.html and
https://gophertrunk.org/acars.html.

## Standalone verification

The following commands write all generated files to Projects. The second build
uses the original OnAir channel source and a small test shim that selects its
default FIR and the same strict block checker; it is not the full OnAir app and
does not exercise its repair implementation.

```sh
mkdir -p /Volumes/Projects/decodium-sdr-build-acars
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-acars \
  clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc \
  src/core/AsyncIqDecoder.cpp src/core/AcarsReceiver.cpp src/dsp/acars/acars_chan.cpp \
  tests/standalone/acars/test_acars.cpp -o /Volumes/Projects/decodium-sdr-build-acars/test_acars_asan
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-acars \
  /Volumes/Projects/decodium-sdr-build-acars/test_acars_asan

DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-acars \
  clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Itests/standalone/acars/onair-shim -I/Volumes/Projects/OnAir-reference-3.9.0/core/include -Isrc \
  src/core/AsyncIqDecoder.cpp src/core/AcarsReceiver.cpp \
  /Volumes/Projects/OnAir-reference-3.9.0/core/src/acars/acars_chan.cpp \
  tests/standalone/acars/test_acars.cpp -o /Volumes/Projects/decodium-sdr-build-acars/test_acars_upstream_asan
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-acars \
  /Volumes/Projects/decodium-sdr-build-acars/test_acars_upstream_asan
```

On macOS, both builds passed with ASan/UBSan: independent synthetic AM IQ at
1 MS/s and offsets 0 and 23 kHz, direct-versus-worker payload equality, chunk
sizes, bad BCS, bad parity with valid BCS, invalid layout with valid BCS,
silence, invalid sample rate, invalid and oversized submission, stale source
generation, source gap mid-block, recovery, nonfinite input and stop. Results
are limited to these vectors. Clock error, sensitivity, overlapping channels,
long traffic, physical hardware, live RF, recorded IQ, other OSes, app lifecycle
and full OnAir feature parity have not been demonstrated.
