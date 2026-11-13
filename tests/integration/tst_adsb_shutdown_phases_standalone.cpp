// SPDX-License-Identifier: GPL-3.0-or-later
// Prototype of an owner-controlled shutdown; no SessionManager changes.
#include "core/AdsbReceiver.h"
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
using dsdr::core::AdsbReceiver;

struct Gate {
    std::atomic<bool> entered{false}, release{false}, exited{false};
    static void hold(void *context) noexcept {
        auto &g = *static_cast<Gate *>(context);
        g.entered.store(true, std::memory_order_release);
        while (!g.release.load(std::memory_order_acquire)) std::this_thread::yield();
        g.exited.store(true, std::memory_order_release);
    }
};
static std::unique_ptr<AdsbReceiver> prepare(Gate &gate) {
    auto receiver = std::make_unique<AdsbReceiver>(Gate::hold, &gate);
    receiver->setSource(true, false, 2000000, 1090000000);
    receiver->setEnabled(true);
    dsdr::dsp::Complex sample{};
    receiver->process(&sample, 1);
    auto end = std::chrono::steady_clock::now() + 5s;
    while (!gate.entered.load(std::memory_order_acquire)) {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::yield();
    }
    return receiver;
}
struct Observation { int heartbeatsWhileHeld = 0; bool completedWhileHeld = false; bool cleaned = false; };
static Observation exercise(QCoreApplication &app, bool phased) {
    Gate gate;
    auto receiver = prepare(gate);
    Observation result;
    std::atomic<bool> requested{false}, completed{false};
    bool closing = false;
    // The owner/reaper exists before the close request. Its receiver has no
    // producer or UI users after the request in this isolated experiment.
    std::thread reaper;
    if (phased) {
        reaper = std::thread([owned = std::move(receiver), &requested, &completed]() mutable {
            while (!requested.load(std::memory_order_acquire)) std::this_thread::sleep_for(1ms);
            owned.reset(); // joins held decoder here, never in the event loop
            completed.store(true, std::memory_order_release);
        });
    }
    std::thread release([&gate] {
        std::this_thread::sleep_for(300ms);
        gate.release.store(true, std::memory_order_release);
    });
    QTimer heartbeat;
    heartbeat.setInterval(5);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
        if (closing && !gate.release.load(std::memory_order_acquire)) ++result.heartbeatsWhileHeld;
        if (completed.load(std::memory_order_acquire)) {
            result.completedWhileHeld = !gate.exited.load(std::memory_order_acquire);
            result.cleaned = true;
            app.quit();
        }
    });
    heartbeat.start();
    QTimer::singleShot(0, &app, [&] {
        closing = true;
        if (phased) requested.store(true, std::memory_order_release);
        else {
            receiver.reset(); // baseline: blocks the event loop in the join
            completed.store(true, std::memory_order_release);
        }
    });
    QTimer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(5000);
    QObject::connect(&watchdog, &QTimer::timeout, &app, [&] { gate.release.store(true); app.quit(); });
    watchdog.start();
    app.exec();
    heartbeat.stop(); watchdog.stop();
    gate.release.store(true, std::memory_order_release);
    release.join();
    if (reaper.joinable()) reaper.join();
    assert(gate.exited.load(std::memory_order_acquire));
    assert(result.cleaned && !result.completedWhileHeld);
    return result;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto baseline = exercise(app, false);
    const auto phased = exercise(app, true);
    assert(baseline.heartbeatsWhileHeld == 0);
    assert(phased.heartbeatsWhileHeld >= 5);
    std::printf("shutdown A/B: baseline ticks=%d, phased ticks=%d; cleanup complete\n",
                baseline.heartbeatsWhileHeld, phased.heartbeatsWhileHeld);
}
