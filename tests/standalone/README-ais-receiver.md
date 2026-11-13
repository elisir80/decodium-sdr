# AIS asynchronous receiver, controller and dialog

These files are standalone and are not registered with the app or shared
CMake files:

- `src/core/AisReceiver.{h,cpp}` runs one `AisDecoder` on an `AsyncIqDecoder`
  worker. It accepts already filtered complex IQ for one AIS channel; the
  decoder does not filter the channel. It has bounded input and output queues,
  uses 64-bit generation tags, and counts input drops, output drops and worker
  exceptions. The output queue capacity is rounded up to a power of two.
- `src/core/AisController.{h,cpp}` runs on the UI thread and polls messages.
  It presents type, nine-digit MMSI, position if valid, status and counters.
- `src/app/qml/AisDialog.qml` displays these fields and enables the receiver;
  it does not configure the source or change tuning.

## Contract for future app wiring

Create one receiver per filtered AIS channel, controller after receiver, and
call `start()` outside the producer and GUI paths. Publish source availability,
rate and offset with `setSource(present, rate, offset)` from serialized control
code. Valid rates are 48–384 ksample/s and offset must leave 15 kHz to the
Nyquist limit. Set `present=false` on source loss. When frequency or source
identity changes without a change in rate/offset, call `discontinuity()`.

Use exactly one IQ producer and one message consumer per receiver. Capture
`generation()` **before acquiring samples**, then use `submitForGeneration()`;
this prevents old IQ acquired before a retune from being tagged as new. Signal
other discontinuities or losses explicitly. Full, invalid and oversized input
is rejected immediately and invalidates the current generation; queued old
blocks and results are discarded. UI control calls are atomic/nonblocking and
must be serialized with one another. The decoder's input is copied into a
preallocated SPSC queue; it does not block the producer.

The host is responsible for preventing stale audio/IQ attribution across source
changes, for channel filtering and for lifecycle. `start()`, `stop()` and
receiver destruction require producer and consumer quiescence; `stop()`
discards queued input and joins the worker and **must not run on the GUI or DSP
thread**. Destroy the controller before the receiver, wait for all callbacks
and raw-pointer users, and coordinate process/backend shutdown with Darwin's
shutdown integration. These files do not install a reaper or prove an app
shutdown barrier. A worker which never returns can prevent shutdown. Constructor
worker hooks are test instrumentation and must be absent in production.

Position extraction supports types 1–4, 9, 11, 17, 18, 19, 21 and 27;
other messages expose type/MMSI without a position. Additional per-type
length and semantic validation are incomplete. No automatic frequency or clock
tracking, SNR/squelch gate, collision handling, NMEA output or authentication
is provided. CRC reduces but cannot eliminate false positives. No hardware,
recorded IQ, GUI app integration or interoperability with another AIS receiver
has been verified.

## Standalone test commands

From the repository root (Qt paths here are for this macOS installation):

```sh
mkdir -p /Volumes/Projects/decodium-sdr-build-ais
/opt/homebrew/share/qt/libexec/moc src/core/AisController.h \
  -o /Volumes/Projects/decodium-sdr-build-ais/moc_AisController.cpp
clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Itests/standalone \
  $(pkg-config --cflags Qt6Core) src/dsp/AisDecoder.cpp \
  src/core/AsyncIqDecoder.cpp src/core/AisReceiver.cpp src/core/AisController.cpp \
  /Volumes/Projects/decodium-sdr-build-ais/moc_AisController.cpp \
  tests/standalone/tst_ais_receiver.cpp $(pkg-config --libs Qt6Core) \
  -o /Volumes/Projects/decodium-sdr-build-ais/tst_ais_receiver
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-ais \
  /Volumes/Projects/decodium-sdr-build-ais/tst_ais_receiver

clang++ -std=c++17 -O1 -g -Wall -Wextra -Werror -pthread -Isrc -Itests/standalone \
  $(pkg-config --cflags Qt6Core Qt6Qml Qt6Quick) src/dsp/AisDecoder.cpp \
  src/core/AsyncIqDecoder.cpp src/core/AisReceiver.cpp src/core/AisController.cpp \
  /Volumes/Projects/decodium-sdr-build-ais/moc_AisController.cpp \
  tests/standalone/tst_ais_qml.cpp $(pkg-config --libs Qt6Core Qt6Qml Qt6Quick) \
  -o /Volumes/Projects/decodium-sdr-build-ais/tst_ais_qml
QT_QPA_PLATFORM=offscreen DSDR_TEST_SILENT_AUDIO=1 \
  TMPDIR=/Volumes/Projects/decodium-sdr-build-ais \
  XDG_CONFIG_HOME=/Volumes/Projects/decodium-sdr-build-ais \
  /Volumes/Projects/decodium-sdr-build-ais/tst_ais_qml \
  /Volumes/Projects/decodium-sdr-build-ais/qml /Volumes/Projects/decodium-sdr/src/app/qml
```

The A/B harness compares the direct decoder with the wrapper on the same
synthetic IQ and bad CRC, tests chunking, slow-worker source/rate/tuning and
enable changes, stale unread results, input saturation, output saturation,
input loss/recovery, and controller output. The QML test uses a minimal Theme
stub and the real controller/receiver; it does not start the full application.
