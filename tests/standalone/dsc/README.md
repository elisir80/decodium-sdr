# DSC RX distress: experimental bitstream nucleus

This isolated receive-only prototype accepts hard DSC bit decisions, searches for strict phasing in either polarity, checks 10-bit symbols, merges DX/RX copies, and verifies the message ECC for a fixed-length distress alert. Output is the 15 raw information symbols, EOS, polarity and count of symbols recovered from one damaged copy. The caller owns timing, sample conversion, lifecycle, and discontinuities; call `reset()` after a gap. Invalid ECC, missing both copies, and valid but conflicting copies are rejected. Strict phasing and a bounded fixed distress layout deliberately sacrifice sensitivity and coverage. This is not a complete DSC implementation and is not connected to the application. In particular it does not receive IQ/audio, demodulate FSK/AFSK, recover clock, support other calls, validate all fields, decode positions into coordinates, or ensure operational distress monitoring. No live RF or real-recording result is claimed.

Run on macOS with a C++17 compiler:

```sh
DSDR_TEST_SILENT_AUDIO=1 tests/standalone/dsc/run.sh
```

No audio device or app is opened. The standalone test builds into `build/dsc-standalone` and reads OnAir from `ONAIR_ROOT`, default `/Volumes/Projects/OnAir-reference-3.9.0`. Both receivers receive the exact same bitstream; the fixture generator independently constructs the symbols, phasing, time diversity and ECC, then checks the result against OnAir's generator. The opposite direction uses OnAir's generator with the local receiver. It checks one known call, 40 varied distress bodies, inversion, one corrupted copy, conflicting copies, bad ECC, truncation, reset and deterministic random noise. The generator and local decoder are newly written, informed by the protocol details and OnAir reference tests and implementation inspected during this task; they are not a clean-room implementation. The A/B test links unmodified OnAir source at build time. Identical tests are not proof of RF sensitivity, complete standards conformance, or performance under realistic channel impairments.

## Reference and notices

Based on OnAir by Kristijonas Adomaitis - https://on-air.dev

Reference: OnAir 3.9.0 commit `15a2907f02f7486fc64b8e439a1645eb7373171f`. The A/B executable includes OnAir `core/src/marine/marine_dsc.cpp` and `marine_mmsi.cpp`, with `core/include/dect2/marine/marine_dsc.h`, `marine_mmsi.h`, `marine_tel.h` and their transitive headers. `REUSE.toml` declares the default GPL-3.0-or-later and copyright (C) 2026 Kristijonas Adomaitis and OnAir contributors; `ADDITIONAL-TERMS.md` imposes the above attribution for covered contributions. `NOTICE.md` requires retention of source notices, licence, additional terms and applicable third-party notices when conveying code. The exact transitive licensing of a distributable binary has not been fully audited: this script is a local experiment and does not package or convey OnAir sources or binaries. Consult the original `LICENSE`, `REUSE.toml`, `NOTICE.md`, `ADDITIONAL-TERMS.md`, `THIRD_PARTY.md` and individual files before distributing a combined work. The local comparison executable prints the required attribution.

OnAir's `tests/real/test_real_dsc.cpp` references `tests/data/marine/dsc_mf_12500_s8.raw`: SSB audio at 12.5 kHz, not an IQ capture. OnAir documents its original recording and licence as unknown. Neither this prototype nor its tests copy or redistribute it, and no A/B test on it has been performed.
