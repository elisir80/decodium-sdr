// SPDX-License-Identifier: GPL-3.0-or-later
// Standalone: c++ -std=c++17 -pthread -Isrc src/core/AsyncIqDecoder.cpp
//   tests/integration/tst_async_iq_decoder_standalone.cpp -o <build-dir>/tst_async_iq_decoder
#include "core/AsyncIqDecoder.h"
#include <atomic>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using dsdr::core::AsyncIqDecoder;
using dsdr::dsp::Complex;
using namespace std::chrono_literals;
struct Sink {
    std::vector<int> values;
    std::vector<std::size_t> resets;
    std::atomic<bool> entered{false}, release{true};
    static void reset(void *p) noexcept {
        auto &s = *static_cast<Sink *>(p); s.resets.push_back(s.values.size());
    }
    static void process(void *p, const Complex *v, std::size_t n) noexcept {
        auto &s = *static_cast<Sink *>(p);
        s.entered.store(true, std::memory_order_release);
        while (!s.release.load(std::memory_order_acquire)) std::this_thread::yield();
        for (std::size_t i = 0; i < n; ++i) s.values.push_back(static_cast<int>(v[i].real()));
    }
    AsyncIqDecoder::Sink callbacks() { return {this, reset, process}; }
};
static void waitFor(const std::atomic<bool> &flag) {
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!flag.load(std::memory_order_acquire)) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}
int main() {
    using R = AsyncIqDecoder::SubmitResult;
    // A: direct decoder, B: same data in irregular asynchronous blocks.
    Sink a, b;
    AsyncIqDecoder q(512, 11, b.callbacks());
    assert(q.submit(nullptr, 1) == R::Stopped);
    q.start();
    Sink::reset(&a);
    int next = 0;
    for (int block = 0; block < 300; ++block) {
        Complex samples[11]; const int n = 1 + (block * 7 % 11);
        for (int i = 0; i < n; ++i) { samples[i] = Complex(float(next), float(-next)); ++next; }
        if (block == 137) { Sink::reset(&a); q.discontinuity(); }
        Sink::process(&a, samples, n);
        assert(q.submit(samples, n) == R::Accepted);
    }
    q.stop();
    assert(a.values == b.values && a.resets == b.resets);
    assert(q.stats().accepted == 300 && q.stats().droppedBlocks == 0);
    q.start();
    Complex last(9999, 0);
    assert(q.submit(&last, 1) == R::Accepted);
    q.stop();
    assert(b.values.back() == 9999 && b.resets.back() == a.values.size());

    // Hold the decoder inside its first callback: the in-flight slot counts
    // against capacity. Producer must return immediately and preserve FIFO.
    Sink slow; slow.release.store(false);
    AsyncIqDecoder overload(2, 2, slow.callbacks());
    overload.start();
    Complex one(1, 0), two(2, 0), three(3, 0), four(4, 0);
    assert(overload.submit(&one, 1) == R::Accepted);
    waitFor(slow.entered);
    assert(overload.submit(&two, 1) == R::Accepted);
    assert(overload.submit(&three, 1) == R::Full);
    assert(overload.submit(&three, 3) == R::TooLarge);
    assert(overload.submit(nullptr, 1) == R::Invalid);
    slow.release.store(true, std::memory_order_release);
    // Wait until slots drain, then the first later block must reset the sink.
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (overload.submit(&four, 1) == R::Full) {
        assert(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    overload.stop();
    assert((slow.values == std::vector<int>{1, 2, 4}));
    assert((slow.resets == std::vector<std::size_t>{0, 2}));
    auto stats = overload.stats();
    assert(stats.accepted == 3 && stats.droppedBlocks >= 3 && stats.droppedSamples >= 5);

    // Race an empty worker against the producer's final submit and drain.
    // Every accepted block must be delivered even if stop follows immediately.
    Sink drain;
    AsyncIqDecoder repeated(4, 1, drain.callbacks());
    for (int iteration = 0; iteration < 2000; ++iteration) {
        repeated.start();
        Complex sample(static_cast<float>(iteration), 0);
        assert(repeated.submit(&sample, 1) == R::Accepted);
        if (iteration % 2 == 0) std::this_thread::yield();
        repeated.stop();
        assert(drain.values.size() == static_cast<std::size_t>(iteration + 1));
        assert(drain.values.back() == iteration);
    }
    std::puts("async IQ decoder A/B and overload: OK");
}
