# AIS staged integration and engine tests

This directory and `docs/patches/ais-registration-candidate.patch` are staged
work. The patch only registers files and tests in CMake; it is deliberately not
applied and does not connect AIS to SessionManager, DspEngine or the main UI.
Re-run `git apply --check` after concurrent changes before considering it.

`AisIqFrontend` translates one channel, applies a Hamming-window FIR and
integer-decimates to 48–96 ksample/s. It accepts input rates from 48 ksample/s
to 2.4 Msample/s, including 2.048 MHz. It reports its actual output rate.
`AisEngineTap` is a producer-side, allocation-free-after-configuration glue
object from that frontend to `AisReceiver`; its receiver must outlive it. The
frontend and tap are not currently constructed or called by the application.
Each simultaneously received AIS channel needs its own tap and receiver.

The producer captures the receiver/fence generation before reading the source
block, then calls `AisEngineTap::process()` with that generation. A bad sample
causes a gap callback, invalidates the generation and drops remaining output
from that source block. The next source block needs a fresh generation. Calls
to configure/reset/process must be serialized on the IQ producer; configure
allocates and belongs in reconfiguration, not the hot path. Receiver controls
must be serialized separately. The owner publishes the frontend's *actual*
output rate as the receiver source rate with offset zero; if frontend
configuration fails, the owner must publish an unavailable source. The filter
runs on the producer and must be benchmarked under full application load.

## Proposed conservative loss fence

`AisIqGapFence` is a separate helper and is not yet wired to the engine. For
one stable `SpscRing<float>` epoch, a loss notification samples that same ring's
`totalWritten()` **in float elements** after the loss has been reported and
calls `notifyLoss()`. `IqFrame::droppedFrames` counts IQ pairs that were not
written; do not add that count to the ring's absolute position. Snapshot the
fence ticket before acquiring samples; capture `totalRead()` immediately
before and after the ring read; pass those float positions to `accepts()`.
Blocks starting before the watermark are dropped whole, including blocks that
straddle it. Follow a successful check with `submitForGeneration()` via the tap.
The watermark may include later samples and thus discard extra data. Odd float
positions invalidate the fence until a quiescent reset because IQ alignment is
no longer assured.

`SpscRing::reset()` starts a new epoch. Before `resetQuiescent()`, disconnect
and drain notifications for the old source and quiesce its producer and
consumer; an old ring pointer/position must never be used for the new epoch.
A source swap/retune also requires invalidating the receiver immediately and
coordinating the source identity, snapshot and pointer lifetime. A late loss
notification cannot retract a frame already displayed before the notification
was observed; queued frames are invalidated, but stronger guarantees require
positioned loss metadata and ordering from the backend. The existing IQ module
ABI does not provide this loss position.

Shutdown remains unintegrated: stop producers and UI users, then coordinate
receiver ownership and worker join with Darwin's `DecoderShutdown` and backend
barriers. Do not call joining lifecycle methods on the GUI or DSP thread. These
staged files do not establish safe full-application exit or PTT/backend closure.

## Tests and scope

1. `tst_ais_frontend` measures selection/rejection at 192 kHz, 1.536 MHz and
   2.048 MHz, including invalid samples and source configurations. Its printed
   throughput is a short local microbenchmark, not a full-load guarantee.
2. `tst_ais_gap_fence` uses actual `SpscRing<float>` counters to exercise a
   cross-fence read, a notification interleaved from another thread, odd
   alignment rejection and a quiescent epoch reset.
3. `tst_ais_end_to_end` generates simultaneous Gaussian-smoothed AIS IQ at
   +/-25 kHz, filters it through `AisEngineTap`, decodes asynchronously and
   checks the actual QML dialog and theme offscreen. It tests disabled/valid/
   bad-FCS, both channels, chunks, stale generation and 2.048 MHz input. This
   test uses the tap directly, not a DspEngine or SessionManager.
4. `tst_ais_real_engine` runs the current unmodified DspEngine with a **test
   fixture** loaded through its existing filtered-IQ module ABI. Synthetic IQ
   passes through the real engine channel DDC/filter and asynchronous AIS worker;
   it checks disabled, invalid CRC, valid positions and retuning between two
   simultaneous AIS signals. This module is only a probe: its ABI lacks complete
   loss metadata and its teardown is not the intended production shutdown path.

All radio input here is generated IQ. There is no hardware, recorded RF,
third-party AIS receiver, production engine tap or full app shutdown evidence.

Run the isolated tests from the repository root:

```sh
cmake -S tests/standalone/ais_stage -B /Volumes/Projects/decodium-sdr-build-ais-stage -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /Volumes/Projects/decodium-sdr-build-ais-stage -j 4
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-ais-stage \
  XDG_CONFIG_HOME=/Volumes/Projects/decodium-sdr-build-ais-stage/config \
  ctest --test-dir /Volumes/Projects/decodium-sdr-build-ais-stage --output-on-failure
```

To build the test against the real engine without editing its shared CMake,
use the test-only top-level CMake injection; all generated files remain under
Projects. Backend choices below make this a local test build, not a release.

```sh
cmake -S /Volumes/Projects/decodium-sdr -B /Volumes/Projects/decodium-sdr-build-ais-engine -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PROJECT_INCLUDE=/Volumes/Projects/decodium-sdr/tests/standalone/ais_stage/engine_test_inject.cmake \
  -DDSDR_BUILD_TESTS=OFF -DDSDR_NEURAL_NR=OFF -DDSDR_BACKEND_SOAPY=OFF \
  -DDSDR_BACKEND_RTLSDR=OFF -DDSDR_BACKEND_AUDIORIG=OFF -DDSDR_BACKEND_COLIBRI=OFF \
  -DDSDR_BUNDLE_SOAPY=OFF -DDSDR_BUNDLE_RTLSDR=OFF -DDSDR_BUNDLE_HAMLIB=OFF
cmake --build /Volumes/Projects/decodium-sdr-build-ais-engine --target tst_ais_real_engine -j 4
DSDR_TEST_SILENT_AUDIO=1 TMPDIR=/Volumes/Projects/decodium-sdr-build-ais-engine \
  XDG_CONFIG_HOME=/Volumes/Projects/decodium-sdr-build-ais-engine/config \
  ctest --test-dir /Volumes/Projects/decodium-sdr-build-ais-engine -R '^tst_ais_real_engine$' --output-on-failure
```
