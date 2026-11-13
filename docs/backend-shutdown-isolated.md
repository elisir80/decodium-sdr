# BackendShutdown: isolated prototype

Status: integrated into the application shutdown path for the audited demo
backend. Other backends retain the existing synchronous fallback. The initial
PTT-off refusal is handled separately by SessionManager before source/services
are stopped; close failures after transfer use the view model retry path.

## Ownership and restrictions

Construct the helper on the GUI thread. The proposed caller must first disable
new operations, stop discovery, fence DSP/TX and all external readers/writers,
disconnect queued callbacks, and retain every dependency. It must also audit
that the concrete backend, its unparented QObjects, internal threads, native
handles and destructor are valid after a Qt thread-affinity move. This cannot
be inferred from the HAL. Call `beginForAuditedBackend()` on the backend's
current thread only after that audit. QObject children are rejected by default
with ownership and parent unchanged. `IndividuallyAuditedTree` is an explicit
per-backend assertion; it must never be used as a generic bypass. The synthetic
demo backend has been exercised with its live ingest thread and parented
QSettings in a standalone test. No physical backend is certified by that test.

On `Accepted` the backend is removed from its parent and moved to a joinable
worker QThread; the caller must never dereference it again. The worker requests
PTT off, checks the backend's reported state, calls `close()`, checks PTT and
open state again, then schedules backend deletion on its affinity thread.
Completion is published on `destroyed`; `readyToExit()` additionally requires
`QThread::wait(0)` to confirm the cleanup thread has terminated. There is no
detached thread. A stuck backend retains ownership and keeps readiness false.
The logical PTT/open getters are backend reports, not physical RF confirmation.

`PttActive`, `BackendStillOpen`, `Exception`, and `QueueFailed` are observable
failure states. No failed state releases ownership or authorizes exit. The GUI
must remain responsive and display a status plus a retry action; `retry()`
queues a new attempt on the same affinity thread. A concurrent or premature
retry is rejected. `cancel()` currently returns false without changing state:
a safe ownership return, reconnection of controllers and producers, and UI
recovery are not implemented. The user cannot currently cancel the real app's
shutdown after a PTT refusal; the future integration must fix that separately.
Do not present rejection of cancellation as an implemented cancellation flow.

Keep the helper alive until `readyToExit()`; its destructor waits if destroyed
prematurely and must not be invoked on the GUI while a backend is stuck. A
process-wide forced exit remains outside the helper's control. A production
coordinator must also wait for all other DSP, decoder, TX and Qt thread barriers.

## Test evidence and manual build

The standalone test uses a fake HAL backend and an offscreen QWindow. It
verifies default refusal of a child, PTT refusal without close, rejected cancel,
successful retry, GUI heartbeat during a held `close()`, close failure with
backend retained, second retry, an explicitly audited synthetic QObject tree
with live child thread and timer, affinity and cleanup. A separate standalone
test exercises the actual demo backend with active ingest, simulated PTT and
an intentionally slow close. Neither test performs physical radio operations.

`BackendShutdownViewModel` and `BackendShutdownDialog.qml` are wired into the
demo shutdown path. The view model exposes retryable close errors and readiness;
the overlay keeps Retry accessible while main content is disabled, and leaves
Cancel disabled. Fake-radio and offscreen QML tests exercise close refusal and
retry. Initial PTT refusal is handled before backend transfer and restores the
UI through SessionManager. Physical backends are not certified by these tests.

```sh
/opt/homebrew/opt/qt/share/qt/libexec/moc -Isrc src/hal/IRadioBackend.h -o /Volumes/Projects/decodium-sdr-async-iq-build/moc_iradio_shutdown.cpp
/opt/homebrew/opt/qt/share/qt/libexec/moc -Isrc src/common/Types.h -o /Volumes/Projects/decodium-sdr-async-iq-build/moc_types_shutdown.cpp
c++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread -Isrc \
  $(pkg-config --cflags Qt6Core Qt6Gui) src/core/BackendShutdown.cpp \
  src/hal/IRadioBackend.cpp /Volumes/Projects/decodium-sdr-async-iq-build/moc_iradio_shutdown.cpp \
  /Volumes/Projects/decodium-sdr-async-iq-build/moc_types_shutdown.cpp \
  tests/integration/tst_backend_shutdown_standalone.cpp $(pkg-config --libs Qt6Core Qt6Gui) \
  -o /Volumes/Projects/decodium-sdr-async-iq-build/tst_backend_shutdown
TMPDIR=/Volumes/Projects/decodium-sdr-async-iq-build/test-tmp \
XDG_CONFIG_HOME=/Volumes/Projects/decodium-sdr-async-iq-build/test-config \
XDG_DATA_HOME=/Volumes/Projects/decodium-sdr-async-iq-build/test-data \
DSDR_TEST_SILENT_AUDIO=1 QT_QPA_PLATFORM=offscreen \
/Volumes/Projects/decodium-sdr-async-iq-build/tst_backend_shutdown
```

Before extending this to physical backends: audit each concrete backend, prove
producer fences and receiver lifetime, implement safe cancellation/return-to-
session, and exercise failures and all app exit paths with real SessionManager
and QML. Never enable the audited-tree policy without a specific backend audit,
including unparented QObject members and native handles.
