# DSC distress MF/HF: worker and dialog experiment

This standalone experiment wires the experimental 24 kHz audio decoder to `AsyncDsc`, a Qt controller and a QML dialog. It is not connected to DspEngine, SessionManager, an audio tap, a radio or the application UI. Support is partial: only a fixed-format MF/HF distress frame; no VHF, automatic tuning, resampling, other DSC call types or validated position/time interpretation. The dialog shows the raw DSC position and UTC symbol pairs, not geographic coordinates or a validated time. ECC is checked by the existing nucleus; it is not a guarantee of operational reliability.

`DSDR_TEST_SILENT_AUDIO=1 tests/standalone/dsc/run_worker.sh` creates a standalone CMake build under `build/dsc-worker-standalone`, starts no audio device, and uses Qt's offscreen platform. It compiles and runs the real QML dialog, feeds an independently generated synthetic FSK recording to OnAir and the worker, checks matching expected MMSI/decoded fields, controller enable and unsupported-rate status, list rendering, source change clearing, explicit gap, automatic gap after a rejected oversized block, recovery on the next call, input saturation accounting and result queue overflow. The A/B reference is linked from the local OnAir tree and the source, attribution and distribution limitations in `README-audio.md` apply. The unknown-origin recording is not accessed.

## Thread and lifecycle contract

One owner serializes `configure`, `start` and `stop` and guarantees the receiver outlives the controller. `submit`, `gap`, `poll` and stats use a mutex; submission copies samples and may allocate, so this interface is not lock-free or suitable for a hard real-time callback. Configure accepts only 24 kHz, 1700 Hz and a non-empty source identifier. Every configuration stops and joins the worker, discards pending input/results, increments generation and requires a fresh start. After configuring, the owner calls `Controller::configurationChanged()` on the UI thread; this clears old display state and starts the receiver if enabled. The controller polls via a 200 ms Qt timer. Sample loss, source changes, retuning and sample-rate changes must be signalled by the owner; the API cannot detect unreported discontinuities. Submission rejects invalid, oversized or full blocks and marks the next accepted block for reset, preventing decoding across the loss. Explicit `gap()` does the same. A stop without drain clears pending blocks and results; drain processes accepted blocks before joining. Input block count/size, result count and dialog history are bounded; result overflow drops new results and exposes a counter. Input counters are cumulative across configuration changes. The internal input queue and vectors allocate dynamically and the worker can be slow; there is no timestamp, backpressure, performance guarantee or measurement of audio samples dropped upstream. A blocked worker delays joining.

The A/B validation exercises an offscreen Qt window and synthetic samples; it is not a live UI, IQ, RF, real recording or hardware test. Integration must still choose a suitable audio source and ensure correct rate, 1700 Hz tuning, source lifecycle and loss notifications.

`run_product_receiver.sh` separately builds the new `src/core/DscReceiver` and
compares a synthetic 48 kHz, 1700 Hz call against OnAir at 24 kHz. It checks disabled
submission, stale-generation rejection, and reception after a generation
change. This product-core component uses the preallocated `AsyncIqDecoder`
queue and downsamples in the worker. `tst_dsc_engine` additionally sends a
synthetic USB IQ signal through the real DSP channel with output muted and
checks disabled/enabled behaviour. The app instantiates the receiver and its
experimental dialog; `tst_dsc_session` checks offscreen opening, channel
selection, enabling, and normal shutdown with a demo source. It also generates
an IQ file and verifies that the expected MMSI reaches the dialog only after
enabling DSC. The file is temporary. The product additionally handles fixed-layout
all-ships and individual MF/HF calls; `run_formats_ab.sh` checks both against
OnAir at bit level, including corrupted ECC, and `run_product_receiver.sh`
compares distress, all-ships, individual/automatic, group, area, distress
acknowledgement and relay on synthetic audio. Bit-level A/B includes corrupted
ECC. The app-level IQ-file test covers each supported case with disabled/enabled
controls and verifies the shown MMSI and type. These tests do not demonstrate
live RF, VHF or other layouts.
