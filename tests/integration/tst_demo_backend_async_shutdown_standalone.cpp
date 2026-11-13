// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BackendShutdown.h"
#include "hal/backends/demo/DemoBackend.h"
#include <QGuiApplication>
#include <QSettings>
#include <QDir>
#include <QTimer>
#include <QWindow>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
using namespace dsdr::core;
using namespace std::chrono_literals;
struct Trace {
    std::atomic<bool> checking{false}, entered{false}, destroyed{false}, badAffinity{false};
    std::atomic<int> pttOff{0};
    QThread *gui = nullptr;
};
class SlowDemo final : public dsdr::hal::demo::DemoBackend {
public:
    SlowDemo(std::shared_ptr<Trace> trace, QObject *parent) : DemoBackend(parent), m_trace(std::move(trace)) {}
    ~SlowDemo() override { verify(); m_trace->destroyed.store(true); }
    void verify() {
        if (!m_trace->checking.load()) return;
        auto *settings = findChild<QSettings *>();
        if (QThread::currentThread() == m_trace->gui || QThread::currentThread() != thread()
            || !settings || settings->thread() != QThread::currentThread()) m_trace->badAffinity.store(true);
        for (auto *child : children())
            if (child->thread() != QThread::currentThread()) m_trace->badAffinity.store(true);
    }
    void setPtt(bool tx) override {
        if (!tx) { verify(); ++m_trace->pttOff; }
        DemoBackend::setPtt(tx);
    }
    void close() override {
        if (isOpen()) {
            verify(); m_trace->entered.store(true);
            std::this_thread::sleep_for(300ms);
        }
        DemoBackend::close();
    }
private:
    std::shared_ptr<Trace> m_trace;
};
template<class F> void waitFor(F f) {
    auto end=std::chrono::steady_clock::now()+5s;
    while (!f()) { QCoreApplication::processEvents(); assert(std::chrono::steady_clock::now()<end); std::this_thread::sleep_for(1ms); }
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto settings = qEnvironmentVariable("DSDR_TEST_SETTINGS_DIR");
    if (!QDir::cleanPath(settings).startsWith(QStringLiteral("/Volumes/Projects/")) || !QDir().mkpath(settings)) return 77;
    QCoreApplication::setOrganizationName(QStringLiteral("DSDR-shutdown-test"));
    QCoreApplication::setApplicationName(QStringLiteral("demo-affinity"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings);
    QWindow window; window.resize(320,160); window.show();
    int beats=0; QTimer timer; timer.setInterval(5);
    QObject::connect(&timer,&QTimer::timeout,&app,[&]{++beats;}); timer.start();
    QObject owner; auto trace=std::make_shared<Trace>(); trace->gui=QThread::currentThread();
    auto *backend=new SlowDemo(trace,&owner);
    auto *settingsChild=backend->findChild<QSettings *>(); assert(settingsChild && settingsChild->parent()==backend);
    backend->open(dsdr::hal::demo::DemoBackend::syntheticDevices().first());
    assert(backend->isOpen());
    backend->setPtt(true); assert(backend->ptt());
    if (qEnvironmentVariableIntValue("DSDR_TEST_SYNC_BASELINE") == 1) {
        const int before = beats;
        backend->setPtt(false);
        backend->close();
        const int during = beats - before;
        delete backend;
        assert(during == 0 && trace->destroyed.load() && trace->pttOff.load() == 1);
        std::printf("demo backend sync baseline: GUI heartbeat=%d during close\n", during);
        return 0;
    }
    BackendShutdown shutdown;
    assert(shutdown.beginForAuditedBackend(backend)==BackendShutdown::BeginResult::HasChildren);
    assert(backend->parent()==&owner && backend->isOpen());
    trace->checking.store(true);
    assert(shutdown.beginForAuditedBackend(backend, BackendShutdown::ChildPolicy::IndividuallyAuditedTree)
           ==BackendShutdown::BeginResult::Accepted);
    waitFor([&]{return trace->entered.load();});
    const int before=beats;
    waitFor([&]{return shutdown.readyToExit();});
    assert(beats-before>=5 && trace->destroyed.load() && !trace->badAffinity.load() && trace->pttOff.load()==1);
    std::printf("demo backend async close: GUI heartbeat=%d; PTT off, Qt affinity and cleanup OK\n", beats-before);
}
