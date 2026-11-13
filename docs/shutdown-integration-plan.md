# Plan: application shutdown with ADS-B, APRS and fake radio

Status: partial DSP/TX fence phase implemented in SessionManager/DspEngine/QML.
It releases logical PTT, clears the DSP source, fences DSP and TX, then uses the
existing synchronous disconnect/backend close. QML close/menu wait for the
fences with the event loop alive. Backend close, decoder joins and general
application shutdown are not yet asynchronous; do not call this complete.

## Current observations (2026-11-13)

* If a backend reports that PTT remains asserted after an off request, the
  application now cancels the pending shutdown before detaching sources or
  stopping services and re-enables interaction. The operator can retry. The
  registered fake-radio A/B tests cover normal release and refused-then-retry;
  this is not proof of physical PTT behaviour. A later backend-close refusal
  still needs an accessible recovery path.

* `SessionManager::~SessionManager()` deletes UI controllers, invokes
  `disconnectDevice()` and `teardownBackend()`, then quits and waits for
  neural, TX and DSP threads on its caller. `disconnectDevice()` requests PTT
  off before backend close. These are synchronous operations; their completion
  and UI responsiveness have not been proven for held workers.
* `DspEngine` owns ADS-B by value; APRS is a `unique_ptr` retired to its own
  static `AprsReaper`. That reaper has a joinable `std::thread`, not a detached
  thread; its static destructor joins it at process exit. If an APRS callback
  never returns, process exit can wait indefinitely. It has no completion
  barrier exposed to the application shutdown path.
* `DecoderShutdown` is still unintegrated. Its cleanup thread is now an owned
  `QThread`: `readyToExit()` also requires nonblocking `wait(0)` to confirm
  thread completion. Its destructor deliberately waits if called prematurely;
  the GUI must keep the helper alive and poll readiness before destruction.
* Main window close and menu quit now request the fence phase; OS quit,
  startup-failure, forced exit and remaining thread cleanup still need coverage.
* The fake-radio baseline additionally delays `close()` on the GUI thread:
  the event-loop heartbeat stops independently of the held-DSP scenario. Demo
  close uses a blocking queued call and thread wait; Soapy and RTL-SDR close
  can wait up to 10 s plus 5 s. Merely moving decoder joins off the GUI does
  not make backend shutdown asynchronous. No generic HAL completion signal or
  PTT-off acknowledgement exists; thread affinity and backend-specific close
  semantics must be addressed before this barrier can be made safe.

## Integration sequence

1. Make shutdown an explicit, idempotent application state while the event
   loop remains active. Reject new radio/TX/source/UI operations; intercept
   all quit routes. A forced process exit is outside application control.
2. Immediately request PTT release and stop TX; require backend-specific
   acknowledgement where available. Stop/close the backend without dropping
   its lifetime. Report failure and remain pending if safe closure cannot be
   established; do not treat an optimistic property write as physical proof.
3. Disconnect UI controllers and timers. Stop new DSP submissions, fence
   queued callbacks and wait asynchronously for producer-quiescence acks.
   Keep QML-visible objects alive or explicitly invalidate bindings safely.
4. Coordinate ownership with Banach: ADS-B currently cannot be transferred
   independently while embedded by value, and APRS already has a separate
   reaper. Choose one owned cleanup path, move both receivers plus callback
   contexts only after every user is quiescent, and avoid competing reapers.
   The `DecoderShutdown` API's acknowledgements are caller assertions, not
   proof of backend state or absence of raw pointers.
5. Request helper shutdown, acknowledge backend closed and users quiescent
   only after the preceding barriers. Poll `readyToExit()` from the event loop;
   it stays false for held decoders and until its QThread has terminated.
6. Quit/join the remaining DSP, neural, TX and backend threads outside the GUI
   waiting path; preserve their referenced buffers and QObject affinity. Keep
   SessionManager, thread handles and cleanup owner alive until every barrier
   and thread termination are proven. Only then allow application exit.
7. Preserve a safe synchronous destructor fallback for non-UI callers, but
   never invoke that fallback on the GUI in the intended asynchronous path.

## Tests to complete after integration

* `tst_shutdown_fake_radio_standalone.cpp` is an existing-path baseline: it
  registers a synthetic demo-derived radio, directly keys fake PTT (no mic/RF),
  then checks PTT-off precedes backend close. A held DSP event verifies the
  current SessionManager destructor blocks the event-loop heartbeat until DSP
  release. This is a baseline exposing the missing asynchronous shutdown, not
  a test of a real window or of held decoder callbacks. Use isolated paths.
* Inject a fake radio plus test hooks to hold ADS-B and APRS workers. Trigger
  actual window close and menu/OS quit on the application event loop. Verify a
  heartbeat continues, repeated close requests are idempotent, and no object is
  destroyed while any queued producer/callback may still access it.
* Delay or fail PTT-off and backend close independently: no exit or decoder
  cleanup may be acknowledged early. Record actual ordering and retain the
  backend until its close acknowledgement. Exercise a permanently held worker
  with a bounded test observation, then release it and verify full cleanup.
* Verify all owned QThreads/reapers have finished before exit, with no detached
  cleanup thread. Repeat a normal no-delay shutdown and startup-failure path.
  Run with AddressSanitizer and, where available, ThreadSanitizer.
* Add standalone tests to CMake only after coordinating the shared build files.
  Run serially if rigctl port 4532 is involved; use `DSDR_TEST_SILENT_AUDIO=1`.

## Test path isolation on macOS

Use dedicated `TMPDIR`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`,
`DSDR_TEST_SETTINGS_DIR` and `DSDR_TEST_APPDATA_DIR` under `/Volumes/Projects`.
Do not assign or reuse the system home environment variable. Qt on macOS can
ignore XDG for `AppDataLocation`; the explicit test override is used for the
model store, band conditions, IQ module lookup and DeepFilter library lookup.
The fake-radio test uses a test-specific `QSettings::setPath`, verifies that
both supplied paths are under Projects, and checks model and conditions paths.
The override is absent in ordinary runs, which retain the Qt defaults.
