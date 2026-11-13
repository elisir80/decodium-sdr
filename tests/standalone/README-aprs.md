# APRS AX.25 AFSK 1200

`src/dsp/AprsAfskDecoder.{h,cpp}` is an optional, standalone receive-only
Bell 202 decoder. It accepts contiguous mono float audio at 8–192 kHz and
emits AX.25 UI frames with PID F0 and a valid CRC-16/X-25, excluding the FCS.
Addresses, control, PID and information remain in the frame; APRS payload
interpretation and presentation are left to the caller. A callback executes
synchronously on the processing thread. Silence, invalid FCS and non-UI frames
produce no callback. Reset after tuning, channel/source changes or discontinuity;
configure again if the sample rate changes. Instances are not thread-safe.

The receiver uses 12 fixed symbol-timing hypotheses, a one-symbol quadrature
energy detector and NRZI/HDLC decoding. It does not track clock drift, perform
error correction, or provide a carrier/quality estimate; the sample-rate range
is not a guarantee of reception under noise, offset or varying levels. Real RF,
recordings and interoperability with external stations have not been validated.

## Integration left to the owner

- Add the decoder source to the DSP target and its include path. No shared
  CMake file is changed by this feature.
- Keep one instance per selected channel, only when APRS is enabled. Feed the
  demodulated channel audio, not raw IQ. In `DspEngine.cpp`, the output of
  `channel.processor->processStereo` is interleaved L/R, before mixing with
  other channels; convert that channel to mono and pass `usable` samples at
  `kInternalAudioRate`. Check the channel mode and audio filters for suitable
  FM packet reception, and reset on retune or stream discontinuity.
- Queue or copy callback frames to the UI thread; do not touch UI objects from
  the DSP callback. Add the desired opt-in UI, settings, APRS parser and tests
  in a separate integration change. None are wired up here.

## Isolated test

From the repository root (build output stays on Projects):

```sh
mkdir -p /Volumes/Projects/decodium-sdr-build-aprs
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Isrc src/dsp/AprsAfskDecoder.cpp \
  tests/standalone/tst_aprs_afsk.cpp \
  -o /Volumes/Projects/decodium-sdr-build-aprs/tst_aprs_afsk
/Volumes/Projects/decodium-sdr-build-aprs/tst_aprs_afsk
```

The A/B test uses generated continuous-phase audio, AX.25 addresses including
a digipeater, bit stuffing, NRZI and a standard CRC check vector. It compares
valid frames against payload and FCS corruption at several rates and offsets,
including chunked streaming, repeated packets and reset during a packet.
