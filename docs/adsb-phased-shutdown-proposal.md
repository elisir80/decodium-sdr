# ADS-B phased shutdown: isolated proposal

Status: the isolated prototype below has since informed a production change:
SessionManager owns ADS-B through its asynchronous decoder shutdown barrier,
after the DSP and backend fences. A held-worker fake-backend integration test
checks event-loop responsiveness and completion ordering. This does not prove
physical PTT release or cover all operating systems and application exit paths.

`tests/integration/tst_adsb_shutdown_phases_standalone.cpp` compares the
current receiver destruction on a Qt event-loop thread with a pre-started
cleanup thread. A deliberately held ADS-B worker makes the join observable:
the event loop must continue servicing a heartbeat until the hook is released;
the receiver and hook context are retained until the join has completed.
The test uses no backend or radio. Compile it without editing CMake:

```sh
c++ -std=c++17 -O2 -Wall -Wextra -Werror -pthread -Isrc \
  $(pkg-config --cflags Qt6Core) \
  src/core/AsyncIqDecoder.cpp src/core/AdsbReceiver.cpp src/dsp/AdsbDecoder.cpp \
  tests/integration/tst_adsb_shutdown_phases_standalone.cpp \
  $(pkg-config --libs Qt6Core) \
  -o /Volumes/Projects/decodium-sdr-async-iq-build/tst_adsb_shutdown_phases
DSDR_TEST_SILENT_AUDIO=1 /Volumes/Projects/decodium-sdr-async-iq-build/tst_adsb_shutdown_phases
```

A production integration should be coordinated with APRS and cover all of the
following, not just transfer ownership of the ADS-B receiver:

1. Intercept all application exit paths while the GUI event loop is alive;
   make shutdown idempotent and disable new source/TX operations.
2. Preserve and verify the existing PTT-off, TX stop and backend close order.
   Keep the backend and every buffer it uses alive until their own stop has
   completed. Do not infer physical PTT release from a software property.
3. Invalidate ADS-B generation, disable GUI polling and disconnect the
   controller. Stop the IQ producer and receive an acknowledgement on the
   DSP thread that no callback can still access the receiver. Account for
   pending queued calls and every raw pointer held outside the decoder.
4. Transfer sole ownership of the entire receiver, its decoder and queue, and
   any callback context to a cleanup executor already running before shutdown.
   Request stop without waiting on the GUI. The executor performs the join and
   destroys those objects; a blocked callback must leave them alive.
5. Deliver completion back to the GUI without retaining dangling QObject
   pointers. Keep SessionManager, QML-facing objects, thread owners, and any
   other referenced resources alive until all relevant workers and backends
   have completed. Finish or join thread handles away from the GUI, including
   the cleanup executor itself. A completion flag set just before a thread
   returns does not by itself establish that the thread has fully exited.
6. Destroy Qt objects in their affinity threads and quit the event loop only
   after the lifecycle barrier. Retain a safe synchronous fallback for callers
   that destroy a session without first running the shutdown protocol; it may
   block and therefore must not be presented as the asynchronous path.

Required integration regressions: retained ADS-B and APRS workers, GUI event
heartbeat, repeated shutdown, disconnect/reconnect, pending DSP callbacks,
source changes and stale generations, a fake TX backend recording PTT-off and
close ordering, and eventual cleanup after release. Test normal window close,
menu quit, startup failure and OS/application quit paths. A permanently stuck
worker must keep ownership intact and leave shutdown pending; it must never be
detached with pointers to destroyed objects.
