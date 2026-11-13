// SPDX-License-Identifier: GPL-3.0-or-later
// Baseline integration against current SessionManager. No physical radio.
#include "core/SessionManager.h"
#include "core/AdsbController.h"
#include "core/AdsbReceiver.h"
#include "core/AprsReceiver.h"
#include "core/AprsController.h"
#include "core/SpectrumFeed.h"
#include "dsp/neural/ModelStore.h"
#include <QFileInfo>
#include "hal/BackendRegistry.h"
#include "hal/backends/demo/DemoBackend.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QWindow>
#include <QElapsedTimer>
#include <QSettings>
#include <QDir>
#include <QTimer>
#include <algorithm>
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <functional>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
using namespace dsdr;
using namespace dsdr::core;
struct Trace {
    std::vector<QString> events;
    std::mutex mutex;
    void record(const QString &event) { std::lock_guard<std::mutex> guard(mutex); events.push_back(event); }
    std::atomic<bool> active{false}, pttState{false}, destroyed{false};
    bool slowClose = false;
    std::atomic<bool> refuseClose{false}, closeRefused{false}, refusePttOff{false};
    std::atomic<bool> insideClose{false};
};
struct AdsbGate {
    std::atomic<bool> entered{false},release{false};
    static void hold(void *p) noexcept {
        auto &g=*static_cast<AdsbGate *>(p); g.entered=true;
        while(!g.release.load()) std::this_thread::yield();
    }
};
class FakeRadio final : public hal::demo::DemoBackend {
public:
    FakeRadio(std::shared_ptr<Trace> trace, QObject *parent) : DemoBackend(parent), m_trace(std::move(trace)) {}
    ~FakeRadio() override { m_trace->destroyed.store(true, std::memory_order_release); }
    void setPtt(bool on) override {
        if (!on && m_trace->refusePttOff) {
            m_trace->record(QStringLiteral("ptt-off-refused"));
            return;
        }
        m_trace->record(on ? QStringLiteral("ptt-on") : QStringLiteral("ptt-off"));
        DemoBackend::setPtt(on);
        m_trace->pttState.store(ptt(), std::memory_order_release);
    }
    void open(const hal::DeviceDescriptor &device) override {
        DemoBackend::open(device); m_trace->active = isOpen();
        m_trace->record(QStringLiteral("open"));
    }
    void close() override {
        if (isOpen()) {
            if (m_trace->refuseClose) {
                m_trace->closeRefused = true;
                m_trace->record(QStringLiteral("close-refused"));
                return;
            }
            m_trace->record(ptt() ? QStringLiteral("close-tx") : QStringLiteral("close-rx"));
            if (m_trace->slowClose) {
                m_trace->insideClose.store(true, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(300));
                m_trace->insideClose.store(false, std::memory_order_release);
            }
        }
        DemoBackend::close(); m_trace->active = isOpen();
    }
private:
    std::shared_ptr<Trace> m_trace;
};
static bool waitFor(const std::function<bool()> &predicate, int ms = 5000) {
    QElapsedTimer time; time.start();
    while (!predicate() && time.elapsed() < ms) {
        QCoreApplication::processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}
static bool isolatedTestDirectory(const QString &path, const QString &root) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    if (!QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(path) ||
        !QDir::cleanPath(path).startsWith(QDir::cleanPath(root) + QLatin1Char('/'), sensitivity) ||
        !QDir().mkpath(root) || !QDir().mkpath(path)) return false;
    const QString canonicalRoot = QDir(root).canonicalPath();
    const QString canonicalPath = QDir(path).canonicalPath();
    if (canonicalRoot.isEmpty() || canonicalPath.isEmpty()) return false;
    return canonicalPath.startsWith(canonicalRoot + QLatin1Char('/'), sensitivity);
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const QString settingsDir = qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR");
    const QString testRoot = qEnvironmentVariable("DSDR_TEST_BUILD_ROOT");
    if (!isolatedTestDirectory(settingsDir, testRoot)) {
        std::fprintf(stderr, "SKIP: test settings must be isolated under the test build root\n");
        return 77;
    }
    QCoreApplication::setOrganizationName(QStringLiteral("DSDR-shutdown-test"));
    QCoreApplication::setApplicationName(QStringLiteral("shutdown-fake"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir);
    const auto dataDir = QDir::cleanPath(qEnvironmentVariable("DSDR_TEST_APPDATA_DIR"));
    if (!isolatedTestDirectory(dataDir, testRoot)) {
        std::fprintf(stderr, "SKIP: test application data must be isolated under the test build root\n");
        return 77;
    }
    QWindow window;
    window.setTitle(QStringLiteral("Shutdown test"));
    window.resize(320, 160);
    window.show();
    auto trace = std::make_shared<Trace>();
    trace->slowClose = qEnvironmentVariableIntValue("DSDR_TEST_SLOW_BACKEND") == 1;
    trace->refuseClose = qEnvironmentVariableIntValue("DSDR_TEST_REFUSE_BACKEND") == 1;
    trace->refusePttOff = qEnvironmentVariableIntValue("DSDR_TEST_REFUSE_PTT") == 1;
    const bool asyncPhase = qEnvironmentVariableIntValue("DSDR_TEST_ASYNC_SHUTDOWN") == 1;
    const bool externalGate = qEnvironmentVariableIntValue("DSDR_TEST_EXTERNAL_GATE") == 1;
    FakeRadio *fake = nullptr;
    hal::BackendRegistry::instance().registerBackend(
        {QStringLiteral("shutdown-fake"), QStringLiteral("Shutdown fake"), QStringLiteral("test")},
        [&fake, trace](QObject *parent) { fake = new FakeRadio(trace, parent); return fake; });
    {
        AdsbGate adsbGate;
        AdsbGate aprsGate;
        const bool heldAdsb=qEnvironmentVariableIntValue("DSDR_TEST_ADSB_HELD")==1;
        const bool heldAprs=qEnvironmentVariableIntValue("DSDR_TEST_APRS_HELD")==1;
        std::unique_ptr<AdsbReceiver> adsb;
        if(heldAdsb) {
            adsb=std::make_unique<AdsbReceiver>(AdsbGate::hold,&adsbGate);
            adsb->setSource(true,false,2000000,1090000000); adsb->setEnabled(true);
            const dsp::Complex sample{}; adsb->process(&sample,1);
            assert(waitFor([&] { return adsbGate.entered.load(); }));
        }
        std::unique_ptr<AprsReceiver> aprs;
        AprsReceiver *aprsRaw = nullptr;
        if (heldAprs) {
            aprs=std::make_unique<AprsReceiver>(64,4096,128,AdsbGate::hold,&aprsGate);
            aprsRaw=aprs.get();
        }
        auto session = std::unique_ptr<SessionManager>(new SessionManager(nullptr, {}, {}, {}, {}, {}, {}, {}, std::move(adsb), std::move(aprs)));
        if (heldAprs) {
            aprsRaw->selectChannel(1); aprsRaw->setChannelState(1,true,true,48000); aprsRaw->setEnabled(true);
            const dsp::Complex sample{};
            assert(aprsRaw->submit(1,&sample,1)==AsyncIqDecoder::SubmitResult::Accepted);
            assert(waitFor([&] { return aprsGate.entered.load(); }));
        }
        session->adsb()->setEnabled(true); assert(session->adsb()->enabled());
        int committed = 0;
        QObject::connect(session.get(), &SessionManager::shutdownCommitted, &app, [&] { ++committed; });
        if (externalGate) session->setExternalShutdownPending(true);
        assert(session->neuralModels()->directory() == QDir(dataDir).filePath(QStringLiteral("models")));
        session->selectBackend(QStringLiteral("shutdown-fake"));
        session->startDiscovery();
        assert(waitFor([&]{ return session->devices()->rowCount() > 0; }));
        session->connectToDevice(0);
        assert(session->isConnected() && fake && trace->active);
        const bool allDecoders=qEnvironmentVariableIntValue("DSDR_TEST_ALL_DECODERS")==1;
        if(allDecoders) {
            session->ais()->setEnabled(true); session->pocsag()->setEnabled(true);
            session->acars()->setEnabled(true); session->selcal()->setEnabled(true);
            session->dsc()->setEnabled(true); session->navtex()->setEnabled(true);
            session->vor()->setEnabled(true);
            assert(session->ais()->enabled() && session->pocsag()->enabled() && session->acars()->enabled());
            assert(session->selcal()->enabled() && session->dsc()->enabled() && session->navtex()->enabled());
            assert(session->vor()->enabled());
        }
        // Directly key the synthetic backend; no microphone or RF device.
        fake->setPtt(true);
        assert(session->isTransmitting());
        // Hold the actual DSP thread while closing the actual session. A
        // heartbeat demonstrates the current GUI-thread blocking behaviour.
        std::atomic<bool> entered{false}, release{false};
        QObject *engine = session->spectrum()->parent();
        std::thread unblock;
        if (!trace->slowClose) {
            assert(engine && QMetaObject::invokeMethod(engine, [&] {
                entered.store(true, std::memory_order_release);
                while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
            }, Qt::QueuedConnection));
            assert(waitFor([&]{ return entered.load(std::memory_order_acquire); }));
            unblock = std::thread([&] { std::this_thread::sleep_for(std::chrono::milliseconds(asyncPhase ? 800 : 300));
                                        release.store(true, std::memory_order_release); });
        }
        int heartbeatWhileHeld = 0;
        int adsbHeartbeats = 0;
        bool backendClosedWhileAdsbHeld = false;
        int aprsHeartbeats = 0;
        bool backendClosedWhileAprsHeld = false;
        bool closing = false;
        QTimer heartbeat; heartbeat.setInterval(5);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
            if (closing && heldAprs && !aprsGate.release.load()) {
                ++aprsHeartbeats; assert(session && !session->shutdownFencePassed());
                if (trace->destroyed.load() && !trace->active.load()) backendClosedWhileAprsHeld=true;
                if (backendClosedWhileAprsHeld && aprsHeartbeats >= 5) aprsGate.release=true;
            }
            if (closing && heldAdsb && !adsbGate.release.load()) {
                ++adsbHeartbeats; assert(session && !session->shutdownFencePassed());
                if(trace->destroyed.load() && !trace->active.load()) backendClosedWhileAdsbHeld=true;
                if (backendClosedWhileAdsbHeld && adsbHeartbeats >= 5) adsbGate.release=true;
            }
            if (closing && (trace->slowClose ? trace->insideClose.load(std::memory_order_acquire)
                                            : !release.load(std::memory_order_acquire))) {
                ++heartbeatWhileHeld;
                if (asyncPhase) { assert(trace->active.load()); assert(!trace->pttState.load()); }
            }
        });
        heartbeat.start();
        QTimer observer; observer.setInterval(5);
        bool retriedClose = false;
        int gatedTicks = 0;
        QObject::connect(&observer, &QTimer::timeout, &app, [&] {
            if (asyncPhase && session && trace->closeRefused.load() && !retriedClose
                && session->shutdownBackendRetryAllowed()) {
                assert(session->shutdownPending() && !session->shutdownFencePassed());
                assert(trace->active.load() && !trace->pttState.load());
                trace->refuseClose.store(false); retriedClose = true;
                session->retryBackendShutdown();
            } else if (asyncPhase && session && externalGate && session->coreShutdownFencePassed()
                       && !session->shutdownFencePassed() && ++gatedTicks >= 3) {
                session->setExternalShutdownPending(false);
            } else if (asyncPhase && session && session->shutdownFencePassed()) {
                if (externalGate) assert(gatedTicks >= 3);
                observer.stop(); session.reset(); app.quit();
            }
        });
        observer.start();
        QTimer::singleShot(0, &app, [&] {
            closing = true;
            if (asyncPhase) {
                session->requestShutdown();
                if (trace->refusePttOff) {
                    // The attempted shutdown must not tear down the open
                    // source or strand the GUI when PTT remains asserted.
                    assert(!session->shutdownPending());
                    assert(committed == 0);
                    assert(session->adsb()->enabled());
                    assert(!session->shutdownFencePassed());
                    assert(session->isConnected() && fake->isOpen() && fake->ptt());
                    assert(trace->active);
                    trace->refusePttOff = false;
                    session->requestShutdown();
                }
                session->requestShutdown(); // idempotent
                assert(committed == 1);
                assert(!session->adsb()->enabled());
                if(allDecoders) {
                    assert(!session->ais()->enabled() && !session->pocsag()->enabled() && !session->acars()->enabled());
                    assert(!session->selcal()->enabled() && !session->dsc()->enabled() && !session->navtex()->enabled());
                    assert(!session->vor()->enabled());
                }
            } else { session.reset(); app.quit(); }
        });
        QTimer watchdog; watchdog.setSingleShot(true); watchdog.setInterval(30000);
        QObject::connect(&watchdog, &QTimer::timeout, &app, [&] {
            release.store(true, std::memory_order_release);
            adsbGate.release = true; aprsGate.release = true; app.quit();
        });
        watchdog.start();
        app.exec();
        if(heldAdsb) { assert(adsbGate.release.load()); assert(adsbHeartbeats>=5); assert(backendClosedWhileAdsbHeld); }
        if(heldAprs) { assert(aprsGate.release.load()); assert(aprsHeartbeats>=5); assert(backendClosedWhileAprsHeld); }
        heartbeat.stop(); observer.stop(); watchdog.stop();
        if (unblock.joinable()) unblock.join();
        if (asyncPhase) {
            assert(!session); // completion, not watchdog
            if (trace->slowClose) {
                assert(heartbeatWhileHeld >= 5);
                std::printf("async backend close: GUI heartbeat %d during close\n", heartbeatWhileHeld);
            } else {
                assert(heartbeatWhileHeld >= 5);
                std::printf("DSP fence phase: GUI heartbeat %d while DSP held\n", heartbeatWhileHeld);
            }
        } else {
            assert(heartbeatWhileHeld == 0);
            std::puts(trace->slowClose ? "current session close baseline: GUI event loop blocks during backend close"
                                       : "current session close baseline: GUI event loop blocks while DSP is held");
        }
    }
    assert(!trace->active);
    if (trace->closeRefused) std::puts("backend close refusal recovered by retry");
    assert(QFileInfo::exists(QDir(dataDir).filePath(QStringLiteral("condizioni.dat"))));
    auto on = std::find(trace->events.begin(), trace->events.end(), QStringLiteral("ptt-on"));
    auto off = std::find(trace->events.begin(), trace->events.end(), QStringLiteral("ptt-off"));
    auto closed = std::find(trace->events.begin(), trace->events.end(), QStringLiteral("close-rx"));
    assert(on != trace->events.end() && off != trace->events.end() && closed != trace->events.end());
    assert(on < off && off < closed);
    assert(std::find(trace->events.begin(), trace->events.end(), QStringLiteral("close-tx")) == trace->events.end());
    std::puts("fake radio session baseline: PTT off precedes backend close");
    return 0;
}
