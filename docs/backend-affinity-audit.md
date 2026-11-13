# Shutdown backend affinity audit, preliminary

This is an engineering inventory, not approval for arbitrary cross-thread
moves. A production transfer also needs fenced DSP/TX users, disabled backend
callbacks into the GUI, and explicit ownership of all referenced objects.

| Backend | Observed objects and close behaviour | Status |
| --- | --- | --- |
| Demo | Active ingest `QThread` is a child; worker lives on ingest thread. `QSettings` was unparented and is now parented to the backend. Close invokes stop with a blocking queued call then joins ingest. | Standalone demo test passed with active ingest, parented settings, delayed close and simulated PTT; the application path and fake-radio/QML retry were also tested. Full race/sanitizer coverage remains. |
| Soapy | Active ingest `QThread` is a child; close requests stop and waits up to 10 s + 5 s, including termination fallback. Native driver handles live in worker. | Not approved: audit native driver and cancellation semantics; physical device verification required. |
| RTL-SDR | Active ingest `QThread` is a child; close has timeout/termination handling. | Not approved: audit device/worker lifetime and physical hardware separately. |
| NetTCP | Parent backend has child thread; client and sockets live on worker. Close uses blocking queued calls. | Not approved: audit all protocol-specific clients and pending callbacks. |
| IQ file | Child thread; worker has timer; close uses blocking queued stop. | Not approved: audit recording/file ownership and deferred deletion. |
| AudioRig | Child CAT thread and parented publish timer; drivers create serial/socket/process objects. PTT/close use blocking queued calls. | Not approved: PTT acknowledgement and serial ownership need explicit tests. |
| Flex | `QUdpSocket` is a `unique_ptr` created without a QObject parent; timers are parented. | Not approved: moving only the QObject tree would leave socket affinity behind. |
| Hermes | Child ingest thread and worker-owned UDP socket. | Not approved: audit worker, TX and PTT stop order. |
| Colibri | Native library/device lifetime. | Not approved: audit library callback and hardware lifetime. |

Never infer that an object without QObject children has no unparented QObject
members. Only opt in to `IndividuallyAuditedTree` for a concrete backend and
configuration that has passed its lifecycle audit. Other app exit paths must
continue to retain backend ownership and remain open while safety is uncertain.

## Integrated demo status

The demo backend advertises the audited transfer capability, and
SessionManager uses the joinable backend worker for that capability after its
DSP/TX fences. The existing initial PTT-refusal path remains before source and
service shutdown. `MainMenu.qml` hosts the retry dialog in the window overlay.
Backends without that capability still use the previous synchronous close;
this is deliberate until their own audits and tests are complete.

The fake-radio CTest suite covers normal close, initial PTT rejection and retry,
a slow backend, close rejection and retry, and the QML retry component. The
separate `tst_shutdown_full_qml_standalone.cpp` loads the actual compiled
`Main.qml`: it verifies initial PTT refusal restores the UI, then a close
refusal is reported and a click on Retry finishes backend cleanup and window
closure. It is currently built manually, not registered in CTest. Tests used an
existing build before the latest concurrent AIS source edits; repeat after AIS
integration and a clean rebuild. No physical PTT or hardware is tested.
