# Experimental standalone VOR receiver

`src/dsp/VorReceiver.{h,cpp}` provides an optional, isolated CVOR phase experiment, not connected to the application. It accepts 48 ksample/s composite AM audio (including DC or DC removed) or already channel-filtered complex IQ. IQ is envelope detected: the carrier must be inside the input channel, and nearby signals must already have been removed. Do not interleave audio and IQ calls in one stream; reset on a discontinuity or input change. Configuration rejects other rates. The owner must run each instance on one worker thread; methods are not thread-safe. The callback runs synchronously and must not re-enter or destroy the instance. Each result follows 0.25 s warm-up and a non-overlapping 1 s measurement. `valid=false` means one or more simple absolute amplitude checks failed; ignore the angle in such results.

The receiver downconverts the 9960 Hz component, discriminates its FM, and compares its 30 Hz phase with the direct 30 Hz AM component. It reports phase lag of AM relative to FM as a conventional VOR radial, with compensation for the implemented filter delay. There is no station tuning, channel filter, AGC, input normalization, anti-aliasing resampling, adaptive acquisition, Doppler VOR validation, true/magnetic conversion, station database, or Morse identification decoder. The checks are heuristics and can accept interference or otherwise invalid transmissions; absolute gain changes can affect them. No calibration against a real beacon or certified signal generator has been done. **Do not use for navigation or safety decisions.**

Protocol context: [FAA flight inspection manual](https://www.faa.gov/documentLibrary/media/order/nd/8200.1c.pdf) describes 30 Hz AM as variable and 30 Hz FM as reference for CVOR; the [1970 U.S. VOR standard](https://www.govinfo.gov/content/pkg/FR-1970-06-10/pdf/FR-1970-06-10.pdf) specifies their phase agreement at magnetic north and deviation ratio 16. The generator and receiver were implemented independently; no OnAir code was copied. A reverse phase convention or DVOR behaviour has not been established from a real capture.

Run from the project root, keeping output under Projects:

```sh
mkdir -p build/vor-standalone
DSDR_TEST_SILENT_AUDIO=1 c++ -std=c++17 -O2 -Wall -Wextra -Werror src/dsp/VorReceiver.cpp tests/standalone/vor/test_vor.cpp -o build/vor-standalone/test_vor
DSDR_TEST_SILENT_AUDIO=1 build/vor-standalone/test_vor
```

The separate transmitter evaluates a closed-form AM waveform with independently generated IQ carrier and seeded Gaussian noise. The A/B matrix tests 0, 45, 90, 180, 270 and 359 degrees; audio and IQ (1400 Hz carrier offset); noise sigma 0 and 0.05 per sample/component, and chunks 1, 257 and whole input: 72 runs. Both 1 s outputs must be valid within 3 degrees. Additional audio and IQ tests add a keyed 1020 Hz Morse-shaped ident tone at 7% amplitude and check only that three angles remain within 3 degrees; the text is not decoded. Controls remove AM or FM, use silence and noise only, reject an invalid sample rate, and reset midway. Signal amplitude is normalized, noise is not a calibrated RF SNR. The tests do not establish accuracy over signal levels, carrier offsets, channel bandwidths, FM deviations, fading, other stations, real ident cadence or captured RF. There is no UI, SDR integration, measured throughput or hardware evidence.
