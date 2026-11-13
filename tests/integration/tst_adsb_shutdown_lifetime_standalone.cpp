// SPDX-License-Identifier: GPL-3.0-or-later
// Regression probe for the existing synchronous ownership boundary.
#include "core/AdsbReceiver.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <memory>
#include <thread>

using namespace std::chrono_literals;
struct Gate {
    std::atomic<bool> entered{false}, release{false}, exited{false};
    static void hold(void *p) noexcept {
        auto &g = *static_cast<Gate *>(p);
        g.entered.store(true, std::memory_order_release);
        while (!g.release.load(std::memory_order_acquire)) std::this_thread::yield();
        g.exited.store(true, std::memory_order_release);
    }
};
int main() {
    Gate gate;
    auto receiver = std::make_unique<dsdr::core::AdsbReceiver>(&Gate::hold, &gate);
    receiver->setSource(true, false, 2000000, 1090000000);
    receiver->setEnabled(true);
    dsdr::dsp::Complex sample(0, 0);
    receiver->process(&sample, 1);
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!gate.entered.load(std::memory_order_acquire)) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    std::atomic<bool> destroyed{false};
    // Destroy on a separate thread to observe the synchronous join without
    // deadlocking the test. The test owns gate until the join completes.
    std::thread closer([r = std::move(receiver), &destroyed]() mutable {
        r.reset();
        destroyed.store(true, std::memory_order_release);
    });
    std::this_thread::sleep_for(150ms);
    assert(!destroyed.load(std::memory_order_acquire));
    gate.release.store(true, std::memory_order_release);
    closer.join();
    assert(gate.exited.load(std::memory_order_acquire));
    assert(destroyed.load(std::memory_order_acquire));
}
