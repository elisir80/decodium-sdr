# VOR asynchronous adapter and UI components

`VorAsyncReceiver` is an optional adapter over `VorReceiver` and the existing `AsyncIqDecoder`; it is not registered or connected to the application. Configure either composite AM audio or already filtered IQ at 48 kHz, then start and supply blocks using the matching submit method. No demodulation, IQ channel filtering, resampling or source attachment is performed by this adapter. Input queue capacity and maximum block length are fixed at construction. It stores only the latest unread measurement and counts overwritten results. Queue-full, invalid input and oversized blocks mark a discontinuity; an explicit `discontinuity()` is also required whenever samples are lost or the source/tuning changes. A new generation invalidates old queued data and unread results; processing restarts with the VOR detector reset.

Use one producer thread and one result consumer. The producer owns submit and discontinuity. Stop the producer and consumer before configure, start, stop or destruction; serialize these lifecycle operations. `stop()` discards pending input, invalidates results and joins the worker, so do not call it from a real-time callback or assume it is nonblocking. The adapter's producer audio conversion uses a preallocated buffer; queue submission does not allocate or wait. The result slot uses a mutex on worker and consumer. The owner must ensure source and receiver lifetimes through shutdown. Queue statistics are cumulative; wrapper-side invalid/oversized submissions invalidate the generation but are not counted by the underlying queue's dropped-block counter.

`VorController` and `VorDialog.qml` are standalone UI pieces. The controller emits `configurationRequested(enabled, input)` with 0 for audio or 1 for IQ; the host must serialize source changes, configure and run the receiver, feed exactly the chosen source, call `setActive` after attachment, and mark discontinuities including source loss. Merely toggling the dialog does not start a receiver. The host must drive `setActive(false)` on source loss and cannot interpret the displayed validity as navigation integrity. Changing source clears active/valid until the host reattaches. The dialog labels the receiver experimental, reports input, state and drops, and does not claim Morse decoding. No shared CMake, DspEngine or SessionManager changes were made.

Standalone verification on macOS (keep build in Projects):

```sh
DSDR_TEST_SILENT_AUDIO=1 cmake -S tests/standalone/vor_async -B build/vor-async-check -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
DSDR_TEST_SILENT_AUDIO=1 cmake --build build/vor-async-check -j 4
DSDR_TEST_SILENT_AUDIO=1 build/vor-async-check/vor_async_test
DSDR_TEST_SILENT_AUDIO=1 QT_QPA_PLATFORM=offscreen build/vor-async-check/vor_gui_test
```

The test compares synchronous and asynchronous output from the same independently generated audio and IQ signal, checks controller state, source mismatch, invalid sample rate, stopping, bounded-queue overflow and generation invalidation, and verifies that half a second after a discontinuity cannot complete the old measurement before recovery with fresh samples. The offscreen Qt window test opens the actual dialog with a minimal test theme and inspects controls, warning and controller bindings. This does not establish application integration, real-time throughput, actual SDR hardware, RF, or navigation-grade integrity. Other core DSP limitations are in `vor-standalone.md`.
