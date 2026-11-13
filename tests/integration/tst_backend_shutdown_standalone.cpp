// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/BackendShutdown.h"
#include "hal/IRadioBackend.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>
#include <QWindow>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace dsdr;
using namespace dsdr::core;
using namespace std::chrono_literals;
struct Trace {
    std::atomic<bool> ptt{true}, open{true}, refusePtt{true}, refuseClose{false};
    std::atomic<bool> holdClose{false}, closeEntered{false}, destroyed{false}, wrongThread{false};
    std::atomic<int> offCalls{0}, closeCalls{0};
    QThread *gui = nullptr;
};
class FakeRadio : public hal::IRadioBackend {
public:
    FakeRadio(std::shared_ptr<Trace> t, QObject *p) : IRadioBackend(p), t(std::move(t)) {}
    ~FakeRadio() override { check(); t->destroyed.store(true); }
    void check() const { if (QThread::currentThread() == t->gui || QThread::currentThread() != thread()) t->wrongThread.store(true); }
    QString backendId() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("fake"); }
    hal::BackendCapabilities capabilities() const override { return {}; }
    BackendState state() const override { return BackendState::Idle; }
    void startDiscovery() override {} void stopDiscovery() override {}
    void open(const hal::DeviceDescriptor &) override {}
    void close() override {
        check(); ++t->closeCalls; t->closeEntered.store(true);
        while (t->holdClose.load()) std::this_thread::yield();
        if (!t->refuseClose.load()) t->open.store(false);
    }
    bool isOpen() const override { check(); return t->open.load(); }
    hal::DeviceDescriptor currentDevice() const override { return {}; }
    void setCenterFrequency(qint64) override {} qint64 centerFrequency() const override { return 0; }
    void setSampleRate(double) override {} double sampleRate() const override { return 0; }
    ChannelId createRxChannel(const hal::RxChannelConfig &) override { return kInvalidChannel; }
    void destroyRxChannel(ChannelId) override {} QList<ChannelId> channels() const override { return {}; }
    void setFrequency(ChannelId, qint64) override {} void setDemod(ChannelId, DemodMode) override {}
    void setFilter(ChannelId, int, int) override {}
    PanId createPanadapter(const hal::PanConfig &) override { return kInvalidPan; }
    void destroyPanadapter(PanId) override {}
    void setPtt(bool on) override { check(); if (!on) ++t->offCalls; if (on || !t->refusePtt.load()) t->ptt.store(on); }
    bool ptt() const override { check(); return t->ptt.load(); }
    void setTxFrequency(qint64) override {}
    hal::SampleRing *iqStream(ChannelId) const override { return nullptr; }
    hal::SampleRing *audioStream(ChannelId) const override { return nullptr; }
    hal::SampleRing *spectrumStream(PanId) const override { return nullptr; }
private: std::shared_ptr<Trace> t;
};
// Synthetic audited tree: a parented timer and an active parented QThread
// whose worker remains in the child thread. No native or unparented resources.
class TreeRadio final : public FakeRadio {
public:
    TreeRadio(std::shared_ptr<Trace> t, QObject *parent, std::atomic<bool> &valid)
        : FakeRadio(std::move(t), parent), m_valid(valid) {
        m_timer = new QTimer(this);
        m_timer->setInterval(1000);
        m_timer->start();
        m_ingest = new QThread(this);
        m_worker = new QObject;
        m_worker->moveToThread(m_ingest);
        QObject::connect(m_ingest, &QThread::finished, m_worker, &QObject::deleteLater);
        m_ingest->start();
    }
    void close() override {
        if (m_ingest) {
            if (m_ingest->thread() != QThread::currentThread() || m_timer->thread() != QThread::currentThread())
                m_valid.store(false);
            m_timer->stop();
            auto *ingest = m_ingest;
            QMetaObject::invokeMethod(m_worker, [this, ingest] {
                if (QThread::currentThread() != ingest) m_valid.store(false);
            }, Qt::BlockingQueuedConnection);
            m_ingest->quit(); m_ingest->wait(); delete m_ingest; m_ingest = nullptr; m_worker = nullptr;
        }
        FakeRadio::close();
    }
private:
    std::atomic<bool> &m_valid;
    QTimer *m_timer = nullptr;
    QThread *m_ingest = nullptr;
    QObject *m_worker = nullptr;
};
template<class F> void waitFor(F f) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!f()) { QCoreApplication::processEvents(); assert(std::chrono::steady_clock::now() < end); std::this_thread::sleep_for(1ms); }
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QWindow window; window.resize(320,160); window.show();
    int beats=0; QTimer timer; timer.setInterval(5);
    QObject::connect(&timer,&QTimer::timeout,&app,[&]{++beats;}); timer.start();
    auto trace=std::make_shared<Trace>(); trace->gui=QThread::currentThread();
    QObject owner;
    auto *radio=new FakeRadio(trace,&owner);
    auto *child=new QObject(radio);
    BackendShutdown shutdown;
    assert(shutdown.beginForAuditedBackend(radio)==BackendShutdown::BeginResult::HasChildren);
    assert(radio->parent()==&owner && !trace->destroyed.load());
    delete child;
    assert(shutdown.beginForAuditedBackend(radio)==BackendShutdown::BeginResult::Accepted);
    assert(shutdown.beginForAuditedBackend(nullptr)==BackendShutdown::BeginResult::AlreadyStarted);
    waitFor([&]{return shutdown.status()==BackendShutdown::Status::PttActive;});
    assert(trace->ptt.load() && trace->open.load() && trace->closeCalls.load()==0);
    assert(!trace->destroyed.load() && !shutdown.readyToExit());
    assert(!shutdown.cancel()); // fail closed; no ownership handoff
    assert(trace->ptt.load() && trace->open.load() && !trace->destroyed.load());
    trace->refusePtt.store(false); trace->holdClose.store(true); trace->refuseClose.store(true);
    assert(shutdown.retry());
    waitFor([&]{return trace->closeEntered.load();});
    const int prior=beats;
    auto end=std::chrono::steady_clock::now()+150ms;
    while(std::chrono::steady_clock::now()<end) { app.processEvents(); std::this_thread::sleep_for(1ms); }
    assert(beats-prior>=5 && !shutdown.readyToExit() && !trace->destroyed.load());
    assert(!trace->ptt.load() && trace->open.load());
    trace->holdClose.store(false);
    waitFor([&]{return shutdown.status()==BackendShutdown::Status::BackendStillOpen;});
    assert(!shutdown.readyToExit() && !trace->destroyed.load() && !shutdown.cancel());
    trace->refuseClose.store(false);
    assert(shutdown.retry());
    waitFor([&]{return shutdown.readyToExit();});
    assert(!trace->ptt.load() && !trace->open.load() && trace->destroyed.load());
    assert(!trace->wrongThread.load() && trace->offCalls.load()==2 && trace->closeCalls.load()==2);
    assert(!shutdown.retry());
    auto treeTrace=std::make_shared<Trace>(); treeTrace->gui=QThread::currentThread();
    treeTrace->refusePtt.store(false);
    std::atomic<bool> treeValid{true};
    auto *tree = new TreeRadio(treeTrace, &owner, treeValid);
    BackendShutdown treeShutdown;
    assert(treeShutdown.beginForAuditedBackend(tree)==BackendShutdown::BeginResult::HasChildren);
    assert(treeShutdown.beginForAuditedBackend(tree, BackendShutdown::ChildPolicy::IndividuallyAuditedTree)
           ==BackendShutdown::BeginResult::Accepted);
    waitFor([&]{return treeShutdown.readyToExit();});
    assert(treeTrace->destroyed.load() && !treeTrace->wrongThread.load() && treeValid.load());
    std::printf("backend shutdown: PTT refusal/retry, slow close heartbeat=%d, audited child tree cleanup OK\n", beats-prior);
    return 0;
}
