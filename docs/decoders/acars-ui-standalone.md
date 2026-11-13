# ACARS standalone controller and dialog

`AcarsController` polls the worker on the UI thread, exposes state and drop/error
counters, and puts the last 200 validated blocks in a bounded model. It displays
mode, seven-character address, ACK, raw label, block ID, text and ETX/ETB.
Control bytes are shown as hexadecimal markers; received text is rendered as
plain text. The history is cleared when the source generation changes, including
retune, loss or disabling, to avoid attributing old messages to the new source.
The message counter is cumulative for the controller lifetime; the clear button
only clears history. At most 32 results are consumed per poll. The dialog does
not choose a frequency or start, stop or configure the receiver.

These files are isolated and are not registered with the app. The host must
create and start the receiver off the DSP/UI threads, configure the source,
provide its IQ stream, create the controller on the UI thread, and coordinate
shutdown: the receiver must outlive the controller, its timer and all other
users; stopping joins and must not block the UI or DSP thread. See the receiver
contract and the licensing audit in [the ACARS standalone README](acars-onair/README.md).

**Integration condition:** retain the OnAir additional terms and put the exact
line `Based on OnAir by Kristijonas Adomaitis - https://on-air.dev` in the
application's About, Credits or Licence screen (or its applicable CLI legal
notices). The standalone test prints this line; the dialog is not a substitute
for the application's legal notices. No shared application integration is made
by this change.

## Offscreen A/B test

The isolated CMake project does not alter shared build files. From the repository
root on this macOS machine:

```sh
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-acars-ui \
  cmake -S tests/standalone/acars/ui -B /Volumes/Projects/decodium-sdr-build-acars-ui/cmake \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-acars-ui \
  cmake --build /Volumes/Projects/decodium-sdr-build-acars-ui/cmake -j4
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-acars-ui \
  ctest --test-dir /Volumes/Projects/decodium-sdr-build-acars-ui/cmake --output-on-failure
```

The CTest environment selects Qt offscreen, silent audio, and Projects paths
for temporary and Qt configuration/cache files. The test sends the same
independently synthesized IQ to two worker receivers, compares raw output with
model fields and rendered text, checks a bad CRC followed by a valid message,
clicks enable/disable and clear, checks source loss and recovery, and verifies
history eviction. It exercises the real QML dialog and controller, not the full
application. It provides no live RF, recorded-IQ, integration, actual desktop
window, other-platform or accessibility proof.
