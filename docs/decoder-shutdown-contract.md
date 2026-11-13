# DecoderShutdown: integration contract

`DecoderShutdown` is an independent ownership and cleanup barrier, not yet
wired into SessionManager, DspEngine, the application exit path or CMake. It
cannot itself prevent the process from being terminated. In particular, never
call application quit merely because this helper exists: the application must
keep its event loop and its objects alive until `readyToExit()` is true **and**
all of its other shutdown barriers have completed.

## Ownership and thread lifetime

Construct the helper before shutdown, with a `Resources` vector containing
sole owners of the ADS-B and APRS receivers. Each resource can be made with
`DecoderShutdown::own(std::unique_ptr<T>)`, or can derive from `Resource`.
Bundle every callback context and dependency with its receiver; their
member-destruction order must retain dependencies until after the receiver's
worker has joined. Keep the helper and its QThread owner alive until
`readyToExit()` returns true. The GUI never joins a decoder through this helper.

Construction and resource wrapping can allocate, create threads, throw, and
may destroy supplied resources on failure; do not perform construction on a
latency-critical shutdown path. Set up an owner before starting the application
shutdown protocol. The worker is an owned, joinable QThread and invokes no
application callback or backend. If any required acknowledgement is missing,
it retains all owned resources and `readyToExit()` remains false. The
application must retain the handle and keep its event loop alive while pending.
Premature handle destruction invokes a blocking `wait()` as a safety fallback;
do not do this on the GUI thread. After resource destructors complete,
`readyToExit()` uses `wait(0)` and reports true only once the helper thread has
also terminated. It does not certify termination of other threads or backend.

## Required sequence

1. Prevent new connection, TX, source, decoder and exit operations. Request
   shutdown with `request()`; this operation is idempotent and does not wait.
2. Run the existing radio safety protocol: release PTT, stop TX, close the
   backend, and wait for actual backend completion. Only then call
   `acknowledgeBackendStopped()`. No reconnect, PTT or backend activity may
   occur after that acknowledgement. It is a caller assertion; this helper
   does not independently measure radio/PTT state or call `close()`.
3. Disable/disconnect UI controllers and timers. Stop each DSP/audio producer
   and wait for acknowledgements proving no callback, queued event, raw pointer,
   or concurrent reader can still access either receiver. Only then call
   `acknowledgeUsersQuiescent()`. Passing a premature acknowledgement makes
   use-after-free possible: the helper cannot discover external pointers.
4. Both acknowledgements and the request can arrive in any order and may be
   repeated. Only when all three are present does the cleanup thread destroy
   resources and wait for decoder workers. A stuck worker keeps ownership and
   `readyToExit()` false. Poll that method from the GUI event loop; do not call
   a blocking join or destroy another object that waits for these workers.
5. Finish all other DSP, audio, TX, backend, Qt-object and thread-handle
   barriers. Exit only after they and `readyToExit()` have all completed. A
   process-wide forced exit/crash remains outside the helper's control.

A destructor fallback in existing code may still wait if integration skips
this protocol. The helper by itself does not make SessionManager shutdown or
backend operations nonblocking and does not authorize abandoning the backend.
The integration must cover normal window close, menu quit, startup failure,
OS quit and repeated requests.

## Standalone verification

The test uses both real receiver classes with synthetic samples and held worker
hooks, without a radio. It checks each missing barrier, duplicate requests,
GUI-event heartbeat while both workers are held, and cleanup after their
release. Build outside the source tree:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread -Isrc \
  $(pkg-config --cflags Qt6Core) \
  src/core/DecoderShutdown.cpp src/core/AsyncIqDecoder.cpp \
  src/core/AdsbReceiver.cpp src/core/AprsReceiver.cpp \
  src/dsp/AdsbDecoder.cpp src/dsp/AprsAfskDecoder.cpp \
  tests/integration/tst_decoder_shutdown_standalone.cpp \
  $(pkg-config --libs Qt6Core) \
  -o /Volumes/Projects/decodium-sdr-async-iq-build/tst_decoder_shutdown
TMPDIR=/Volumes/Projects/decodium-sdr-async-iq-build/test-tmp \
XDG_CONFIG_HOME=/Volumes/Projects/decodium-sdr-async-iq-build/test-config \
DSDR_TEST_SILENT_AUDIO=1 /Volumes/Projects/decodium-sdr-async-iq-build/tst_decoder_shutdown
```

Before production integration, add a fake backend regression that records PTT
release, TX stop and backend close ordering, and verify GUI responsiveness and
object cleanup in the real application shutdown path. Hardware PTT behaviour
requires separate physical verification.
