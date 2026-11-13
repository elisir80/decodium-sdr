# AIS RX standalone

`src/dsp/AisDecoder.{h,cpp}` is a receive-only AIS GMSK decoder for one
already-filtered complex IQ channel, at 9600 symbols/s. `configure(rate,
offsetHz)` accepts 48–384 ksample/s and an offset relative to the IQ centre;
separate instances are needed for AIS 1 (161.975 MHz) and AIS 2 (162.025 MHz).
The caller must select/filter each channel and provide contiguous IQ samples.
The component is also used by the application's asynchronous AIS receiver.

The decoder uses an FM discriminator, fixed parallel timing hypotheses, NRZI,
HDLC flags and bit unstuffing, and CRC-16/X-25. A callback receives valid
payload bytes in MSB-first AIS message-table order, the message type and MMSI.
For position reports 1–4, 9, 11 and 18 of at least 168 bits, and type 19 of at
least 312 bits, type 17 of 80–816 bits, type 21 of 272–360 bits, and type 27 of at least 96 bits, valid longitude and latitude are also exposed. Type 17 coordinates identify a DGNSS reference station; unavailable or
out-of-range positions remain unset. Other AIS
fields and NMEA AIVDM output are not implemented. The message type and MMSI
are extracted from any CRC-valid message with a supported type number, without
full per-type validation. The current maximum payload is 128 bytes.

There is no channel filter, adaptive frequency correction, timing drift loop,
squelch, collision recovery, NMEA parser or multipart message assembly.
The supplied offset must be sufficiently accurate and input bandwidth must
isolate the desired channel. Noise can produce an occasional CRC false positive;
CRC is not authentication. The callback and all processing run synchronously
on the caller's thread, with dynamic memory allocation. An integration should
use a bounded worker, handle sample loss by `reset()`, and coordinate shutdown
before destroying the decoder. A zero-amplitude break also resets reception.
No network access or transmission is performed by the component.

The separate test generator creates Gaussian-smoothed continuous-phase FSK IQ,
with independent NRZI/bit-stuffing and an MSB-first CRC implementation. Tests
compare good/altered FCS, chunked/whole input, signal polarity, noise, offsets,
48/96/192 ksample/s, stuffing, position sentinels, tuning mismatch and reset.
They include a GPSD type-1 payload with independently published MMSI and
coordinates, re-modulated synthetically; this is a known-message test, not a
recorded RF sample. See `ais-gpsd-BSD-2-Clause.txt` for the GPSD sample's
license. The sample fixture is copyright 2010 the GPSD project, from
https://gitlab.com/gpsd/gpsd/-/raw/master/test/sample.aivdm . The new decoder and
generator are GPL-3.0-or-later. The protocol reference is
https://www.itu.int/dms_pubrec/itu-r/rec/m/R-REC-M.1371-6-202602-I%21%21PDF-E.pdf .
These standalone tests do not establish real RF, hardware or third-party
receiver interoperability. Separate application integration tests are listed
in `docs/decoders/priority-ten.md`.

Run from the repository root, with output on Projects:

```sh
mkdir -p /Volumes/Projects/decodium-sdr-build-ais
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Isrc -Itests/standalone src/dsp/AisDecoder.cpp \
  tests/standalone/tst_ais_decoder.cpp \
  -o /Volumes/Projects/decodium-sdr-build-ais/tst_ais_decoder
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-ais \
  /Volumes/Projects/decodium-sdr-build-ais/tst_ais_decoder
```
