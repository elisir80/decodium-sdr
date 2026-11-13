// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/AsyncIqDecoder.h"
#include <cassert>
#include <chrono>
#include <cstdint>
#include <vector>
using namespace dsdr::core;
using dsdr::dsp::Complex;
struct Sink {
    std::vector<std::uint64_t> tags;
    std::vector<Complex> samples;
    std::vector<std::chrono::steady_clock::time_point> timestamps;
    static void reset(void *) noexcept {}
    static void process(void *p, const Complex *v, std::size_t n, std::uint64_t tag,
                        std::chrono::steady_clock::time_point timestamp) noexcept {
        auto &s = *static_cast<Sink *>(p);
        s.tags.push_back(tag); s.timestamps.push_back(timestamp);
        s.samples.insert(s.samples.end(), v, v+n);
    }
};
int main() {
    Sink sink;
    AsyncIqDecoder q(8, 2, {&sink, Sink::reset, nullptr, Sink::process});
    q.start();
    const Complex a[]{{1,2},{3,4}}, b[]{{5,6}};
    const auto now = std::chrono::steady_clock::now();
    constexpr std::uint64_t high = 0xfedcba9876543210ULL;
    assert(q.submit(a,2,high,now)==AsyncIqDecoder::SubmitResult::Accepted);
    assert(q.submit(b,1,high+1,now + std::chrono::nanoseconds(17))==AsyncIqDecoder::SubmitResult::Accepted);
    q.stop();
    assert((sink.tags == std::vector<std::uint64_t>{high, high+1}));
    assert((sink.samples == std::vector<Complex>{{1,2},{3,4},{5,6}}));
    assert(sink.timestamps[0] == now && sink.timestamps[1] == now + std::chrono::nanoseconds(17));
    assert(q.stats().accepted == 2);
}
