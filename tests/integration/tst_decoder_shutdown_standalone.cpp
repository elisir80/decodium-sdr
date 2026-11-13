// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AdsbReceiver.h"
#include "core/AprsReceiver.h"
#include "core/DecoderShutdown.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
using namespace std::chrono_literals;
using namespace dsdr::core;

struct Gate {
    std::atomic<bool> entered{false}, release{false}, exited{false};
    static void hold(void *ctx) noexcept {
        auto &g = *static_cast<Gate *>(ctx);
        g.entered.store(true, std::memory_order_release);
        while (!g.release.load(std::memory_order_acquire)) std::this_thread::yield();
        g.exited.store(true, std::memory_order_release);
    }
};
struct Witness {
    std::atomic<bool> *destroyed, *offUi;
    std::thread::id ui;
    ~Witness() {
        offUi->store(std::this_thread::get_id() != ui, std::memory_order_release);
        destroyed->store(true, std::memory_order_release);
    }
};
struct Bundle {
    Bundle(std::atomic<bool> *destroyed, std::atomic<bool> *offUi,
           std::shared_ptr<Gate> a, std::shared_ptr<Gate> p)
        : witness{destroyed, offUi, std::this_thread::get_id()},
          adsbGate(std::move(a)), aprsGate(std::move(p)),
          adsb(std::make_unique<AdsbReceiver>(Gate::hold, adsbGate.get())),
          aprs(std::make_unique<AprsReceiver>(8, 128, 8, Gate::hold, aprsGate.get())) {}
    // Reverse destruction: receivers first, witness and gates last.
    Witness witness;
    std::shared_ptr<Gate> adsbGate, aprsGate;
    std::unique_ptr<AdsbReceiver> adsb;
    std::unique_ptr<AprsReceiver> aprs;
};
struct TrackedResource final : DecoderShutdown::Resource {
    TrackedResource(std::unique_ptr<Bundle> value, std::atomic<bool> &started)
        : bundle(std::move(value)), cleanupStarted(started) {}
    ~TrackedResource() override {
        cleanupStarted.store(true, std::memory_order_release);
        bundle.reset();
    }
    std::unique_ptr<Bundle> bundle;
    std::atomic<bool> &cleanupStarted;
};
template<class F> static void waitFor(F predicate) {
    const auto until = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        QCoreApplication::processEvents();
        assert(std::chrono::steady_clock::now() < until);
        std::this_thread::sleep_for(1ms);
    }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto a = std::make_shared<Gate>(), p = std::make_shared<Gate>();
    std::atomic<bool> destroyed{false}, offUi{false}, cleanupStarted{false};
    auto bundle = std::make_unique<Bundle>(&destroyed, &offUi, a, p);
    bundle->adsb->setSource(true, false, 2000000, 1090000000);
    bundle->adsb->setEnabled(true);
    dsdr::dsp::Complex input[16]{};
    bundle->adsb->process(input, 16);
    bundle->aprs->selectChannel(7);
    bundle->aprs->setChannelState(7, true, true, 48000);
    bundle->aprs->setEnabled(true);
    bundle->aprs->start();
    assert(bundle->aprs->submit(7, input, 16) == AsyncIqDecoder::SubmitResult::Accepted);
    waitFor([&]{ return a->entered.load() && p->entered.load(); });
    DecoderShutdown::Resources resources;
    resources.push_back(std::make_unique<TrackedResource>(std::move(bundle), cleanupStarted));
    DecoderShutdown shutdown(std::move(resources));
    int beats = 0;
    QTimer heartbeat; heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&]{ ++beats; });
    heartbeat.start();
    // A: one missing barrier at a time. Backend remains active until its ack.
    shutdown.acknowledgeUsersQuiescent();
    std::this_thread::sleep_for(20ms);
    assert(!cleanupStarted.load() && !destroyed.load() && !shutdown.readyToExit());
    shutdown.request(); shutdown.request();
    std::this_thread::sleep_for(20ms);
    assert(!cleanupStarted.load() && !destroyed.load() && !shutdown.readyToExit());
    shutdown.acknowledgeBackendStopped(); shutdown.acknowledgeBackendStopped();
    waitFor([&]{ return cleanupStarted.load(); });
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < 100) { app.processEvents(); std::this_thread::sleep_for(1ms); }
    assert(beats >= 5 && !shutdown.readyToExit() && !destroyed.load());
    // B: even one retained decoder keeps the bundle owned and exit gated.
    a->release.store(true, std::memory_order_release);
    std::this_thread::sleep_for(30ms);
    assert(!shutdown.readyToExit() && !destroyed.load());
    p->release.store(true, std::memory_order_release);
    waitFor([&]{ return shutdown.readyToExit(); });
    assert(a->exited.load() && p->exited.load() && destroyed.load() && offUi.load());
    std::printf("decoder shutdown barriers: ADS-B + APRS, GUI beats=%d, cleanup complete\n", beats);
}
