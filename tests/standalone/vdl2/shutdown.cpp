// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/DecoderShutdown.h"
#include "core/DspEngine.h"
#include "core/Vdl2Resources.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <memory>
using namespace dsdr::core;
struct Probe {
    std::atomic<bool>* destroyed;
    Vdl2Resources resources{105000,136975000,0,0.001};
    explicit Probe(std::atomic<bool>* flag):destroyed(flag) { resources.session().start(); }
    ~Probe() noexcept { destroyed->store(true); }
};
struct TrackingTap final : RawIqTap {
    Vdl2IqIngress& ingress;
    std::atomic<int> calls{0};
    explicit TrackingTap(Vdl2IqIngress& value):ingress(value) {}
    void process(const float* p,std::size_t n,double rate,std::int64_t center) noexcept override {
        ++calls; ingress.process(p,n,rate,center);
    }
    void discontinuity() noexcept override { ingress.discontinuity(); }
    void invalidateResults() noexcept override { ingress.invalidateResults(); }
};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    std::atomic<bool> destroyed{false};
    auto probe=std::make_unique<Probe>(&destroyed);
    DspEngine engine;
    TrackingTap tap(probe->resources.ingress());
    engine.attachRawIqTap(&tap);
    dsdr::dsp::SpscRing<float> ring(8192);
    float samples[2048]{};
    if(ring.write(samples,2048)!=2048) return 4;
    QThread dspThread; engine.moveToThread(&dspThread); dspThread.start();
    auto* uiThread=app.thread();
    QMetaObject::invokeMethod(&engine,[&] {
        engine.setSource(&ring,105000,136975000,false);
        dsdr::hal::IqFrame frame; frame.frameCount=1024; engine.onIqFrameReady(frame);
    },Qt::BlockingQueuedConnection);
    if(tap.calls.load()!=1) { QMetaObject::invokeMethod(&engine,[&]{engine.attachRawIqTap(nullptr); engine.moveToThread(uiThread);},Qt::BlockingQueuedConnection); dspThread.quit(); dspThread.wait(); return 5; }
    DecoderShutdown::Resources list;
    list.push_back(DecoderShutdown::own(std::move(probe)));
    DecoderShutdown shutdown(std::move(list));
    shutdown.request();
    QThread::msleep(25);
    const bool beforeFence=!destroyed.load() && !shutdown.readyToExit();
    // This queued callback runs after earlier DSP work and fences the tap.
    QMetaObject::invokeMethod(&engine,[&] { engine.clearSource(); engine.attachRawIqTap(nullptr); engine.moveToThread(uiThread); },Qt::BlockingQueuedConnection);
    dspThread.quit(); dspThread.wait();
    shutdown.acknowledgeUsersQuiescent();
    QThread::msleep(25);
    const bool beforeBackend=!destroyed.load() && !shutdown.readyToExit();
    shutdown.acknowledgeBackendStopped();
    QElapsedTimer timer; timer.start();
    while(!shutdown.readyToExit() && timer.elapsed()<5000) QThread::msleep(1);
    if(!shutdown.readyToExit() || !destroyed.load()) return 3;
    return beforeFence && beforeBackend ? 0 : 1;
}
