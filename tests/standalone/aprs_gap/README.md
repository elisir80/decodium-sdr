# APRS queued-IQ loss regression

`tst_aprs_backlog.cpp` exercises the real DspEngine and APRS receiver with the
same generated NFM IQ in two cases: no loss and a loss notification while more
than four processing blocks, including a complete valid packet, remain queued.
The no-loss case must decode that packet; the loss case must not decode the
old queued packet. Both must produce the same nonzero audio, sample for sample,
and a complete packet submitted after the loss must decode successfully.

The test invokes `onIqFrameReady()` directly on the engine's thread after the
backlog has been written. It reproduces a loss notification before the first
processing dispatch and deliberately makes the backlog span several dispatches.
It is not an exhaustive concurrent-backend, hardware or application-shutdown
test. It does not alter or inspect user settings and opens no audio device.

On the isolated pre-fence source snapshot used to develop the test, the audio
comparison and subsequent packet recovery passed, while the loss case
incorrectly decoded one queued pre-loss packet. With the conservative APRS
fence integrated in DspEngine, this same regression passed in the isolated
Release build on macOS. Loss notifications arriving after affected samples
have already been processed are outside the guarantee of this test.

From the repository root, build with the isolated CMake injection (no shared
CMake edits); all generated files stay under Projects:

```sh
cmake -S /Volumes/Projects/decodium-sdr -B /Volumes/Projects/decodium-sdr-build-aprs-gap -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PROJECT_INCLUDE=/Volumes/Projects/decodium-sdr/tests/standalone/aprs_gap/inject.cmake \
  -DDSDR_BUILD_TESTS=OFF -DDSDR_NEURAL_NR=OFF -DDSDR_BACKEND_SOAPY=OFF \
  -DDSDR_BACKEND_RTLSDR=OFF -DDSDR_BACKEND_AUDIORIG=OFF -DDSDR_BACKEND_COLIBRI=OFF \
  -DDSDR_BUNDLE_SOAPY=OFF -DDSDR_BUNDLE_RTLSDR=OFF -DDSDR_BUNDLE_HAMLIB=OFF
cmake --build /Volumes/Projects/decodium-sdr-build-aprs-gap --target tst_aprs_backlog -j 4
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-aprs-gap \
  XDG_CONFIG_HOME=/Volumes/Projects/decodium-sdr-build-aprs-gap/config \
  ctest --test-dir /Volumes/Projects/decodium-sdr-build-aprs-gap -R '^tst_aprs_backlog$' --output-on-failure
```
