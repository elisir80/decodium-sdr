# OnAir 3.9.0: ADS-B IQ experiment (receive only)

Based on OnAir by Kristijonas Adomaitis - https://on-air.dev

This is an isolated, incomplete integration experiment, not an application plugin.
It compiles the unmodified OnAir `adsb_demod.cpp` and `dsp_compat.cpp` from
`ONAIR_ROOT` (default `/Volumes/Projects/OnAir-reference-3.9.0`), alongside
Decodium's `AsyncIqDecoder` and `AdsbDecoder`. OnAir reference commit used:
`15a2907f02f7486fc64b8e439a1645eb7373171f`. C++17 compiler required; Rust
is not required for this experiment. No TX code is linked.

Run `DSDR_TEST_SILENT_AUDIO=1 tests/standalone/onair_vertical/run.sh`. This
standalone program has no audio output or application volume controls. It
sends identical synthetic 2 Msps IQ in 137-sample chunks through the
Decodium decoder and the actual OnAir demodulator, the latter on Decodium's
bounded asynchronous IQ worker. It retrieves OnAir candidate frames on the
worker, validates their CRC with Decodium's decoder, and compares eight frames
with the Decodium output and a known frame. An unsuccessful submission fails
the test. This demonstrates the algorithms and the queue, not IQ delivery by
a running Decodium session. It does not exercise OnAir's tracker, message
parser, or Rust core, real hardware, audio, UI, or live input. It establishes
no decoder parity or RF performance.

The script also compares both demodulators on the real 2 Msps CU8 recording
`tests/data/adsb/modes1_2msps_cu8.bin` in the reference checkout, attributed
there to dump1090. The experiment reports counts of CRC-valid 112-bit DF17/18
frames and distinct matching frames; it does not compare short frames or use
OnAir's tracker, repair logic or complete receiver. The test verifies both
paths recover at least 130 and 100 frames respectively, all from the recording's
known ICAO address; it does not require equal output. Initially Decodium decoded
121 frames (89 distinct), versus 127 (93 distinct) for OnAir. After tuning the
preamble and symbol thresholds, the same run decoded 133 (98 distinct) for
Decodium and 127 (93 distinct) for OnAir, with 80 distinct frames shared. This
single recording is insufficient to establish parity or general sensitivity. Consult
the source dataset's terms before redistributing the recording; this experiment
references the file in place and does not copy it.

Potential attachment: the raw input tap beside the existing ADS-B receiver in
`DspEngine::onIqFrameReady`, before channel filtering, with bounded async
transfer and an independently polled result queue. Existing IQ module ABI v1
is another possible attachment but receives filtered channel baseband, not raw
IQ: OnAir ADS-B requires at least 2 Msps and a sufficiently wide filter, so
rate and bandwidth must be demonstrated, and result transport/lifecycle and
discontinuity handling must be designed. Neither path is wired by this test.
Do not use this test as a deployable module.

OnAir is GPL-3.0-or-later with additional attribution terms; consult its
`LICENSE`, `ADDITIONAL-TERMS.md`, `NOTICE.md`, `REUSE.toml` and
`THIRD_PARTY.md` in the reference checkout when distributing a combined work.
The original OnAir source and its notices remain in that checkout. The exact
attribution above must appear in appropriate legal notices when conveying a
covered work. This experiment does not copy the OnAir files.
