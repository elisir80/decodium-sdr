# APRS asynchronous receiver and UI integration

The decoder, asynchronous receiver, session controller and QML dialog are
integrated. DspEngine feeds the selected NFM channel audio before mixing; the
worker converts stereo to mono and performs APRS decoding.

- `src/core/AprsReceiver.{h,cpp}` uses `AsyncIqDecoder`. Its bounded input
  queue carries one complex sample per stereo frame: real=L, imaginary=R.
  The worker performs the mono downmix and runs `AprsAfskDecoder`. Input
  submission uses preallocated storage; decoding and callbacks run on the worker.
- `src/core/AprsController.{h,cpp}` polls the bounded frame queue on the UI
  thread and exposes selection, status, counters and last AX.25 frame.
- `src/app/qml/AprsDialog.qml` is an opt-in dialog with explicit NFM channel
  selection. It does not tune the radio or transmit.

## Runtime behavior and limits

Select an NFM channel and enable APRS. The tap uses the demodulated channel
audio at 48 kHz, including that channel's volume, mute, squelch and audio
filters: a muted, heavily filtered or squelched channel may yield no frames.
IQ sources using client-side demodulation are supported. Audio-only backends
are not supported by this tap. Retune, source changes, replay changes, channel
settings and reported source or queue losses invalidate old generations.
A backend must report lost samples accurately; without sample-position metadata
the exact position of an upstream loss inside an already queued block is unknown.

The DSP producer only copies to bounded queues; it does not wait for the APRS
worker. After producer and controller quiescence, the engine transfers the
receiver to an owned background reaper. The reaper joins the APRS worker and is
itself joined at process exit. A stuck worker can therefore delay process exit;
the reaper is not a backend shutdown barrier and is not yet coordinated with
Darwin's separate DecoderShutdown helper or ADS-B shutdown.

Each accepted block is tagged with its producer generation. The worker checks
it before decoding; decoded frames are tagged and checked again when read.
Changing selection, state, enablement or calling `discontinuity()` invalidates
previous generations, including blocks and frames already queued. A loss on
submission does the same. The optional worker hook in the constructor is for
instrumentation and testing only; it must never block in production.

The decoder has no symbol-clock drift correction or APRS semantic parser. The
controller displays source, destination, raw information text and the AX.25
frame. Synthetic tests do not establish live RF reception or interoperability.

## Standalone tests

All outputs below stay under Projects. Run from the repository root. The Qt QML
test uses a minimal `DecodiumSdr.Theme` stub in the build directory, so it
checks dialog rendering and controller bindings, not integration into the app.

```sh
mkdir -p /Volumes/Projects/decodium-sdr-build-aprs
/opt/homebrew/share/qt/libexec/moc src/core/AprsController.h \
  -o /Volumes/Projects/decodium-sdr-build-aprs/moc_AprsController.cpp
clang++ -std=c++17 -pthread -Isrc $(pkg-config --cflags Qt6Core) \
  src/dsp/AprsAfskDecoder.cpp src/core/AsyncIqDecoder.cpp \
  src/core/AprsReceiver.cpp src/core/AprsController.cpp \
  /Volumes/Projects/decodium-sdr-build-aprs/moc_AprsController.cpp \
  tests/integration/tst_aprs_receiver_standalone.cpp $(pkg-config --libs Qt6Core) \
  -o /Volumes/Projects/decodium-sdr-build-aprs/tst_aprs_receiver
DSDR_TEST_SILENT_AUDIO=1 /Volumes/Projects/decodium-sdr-build-aprs/tst_aprs_receiver
```

The A/B test compares synchronous mono against asynchronous stereo decode,
validates CRC rejection, explicit selection, queue limits and loss recovery,
and holds the worker while changing channel, retuning and disabling the
receiver. It also verifies previously decoded queued results are discarded.
